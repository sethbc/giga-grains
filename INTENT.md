# Giga Grains — Intent

> The open, CLAP-native granular instrument and effect, at giga scale.

| | |
|---|---|
| **Status** | Intent v1 · 2026-10-26 · M0 (Foundations) done 2026-10-26; M1 (Cloud core) closed 2026-10-29 (§23 #60); the installers and release pipeline built 2026-10-29 (`docs/superpowers/specs/2026-10-29-release-pipeline-design.md`, §23 #61; the runbook is `docs/releasing.md`); M2 (Giga) closed 2026-11-03, seven sub-projects in four waves (`docs/superpowers/specs/2026-10-29-m2-program-design.md`, §23 #64; tagged 0.2.0, an unpublished draft): 1 Layers and structure, PR #15; 3 Engine scale and quality, PRs #16 and #23; 2 Sources and persistence, PR #19; 4 Extremes and the FX chain, PR #20; 5 Presets and content, PRs #17 and #25; 6 Views, PRs #18 and #24; 7 the close, PR #31 (§23 #65, #66, #68 to #71, #93); M3 (Playable) in progress since 2026-11-07, six sub-projects in series (`docs/superpowers/specs/2026-11-07-m3-program-design.md`, §23 #94); nothing is public and there are no betas before M5 (§23 #62); v1.0 follows M5 (§20) |
| **Products** | *Giga Grains* (instrument) and *Giga Grains FX* (effect), sharing one engine |
| **License** | AGPL-3.0-or-later |
| **Director** | Seth Chandler. Claude writes most of the code; the director steers, listens and reviews. |

**How this document works.** Part I (Intent) records *why* and *what*. It is stable and changes
only with the director's explicit sign-off. Part II (Initial design direction) records *how* as of
today and is expected to evolve. Every change to Part II adds a dated entry to the
[decision log](#23-decision-log) in the same change. When work and this document disagree,
either the work changes or the document is amended on purpose. Silent divergence is not allowed.

---

## Part I — Intent

### 1. Vision

Giga Grains combines:

- the **immediacy of Clouds, Beads and Microcosm**: few controls, musical even when pushed;
- a **Borderlands-style live canvas** that shows the grains and lets you play them directly;
- **Portal's speed to a great result**;
- **the depth and MPE playability of Quanta and Granulator III** when you dig in.

It runs in every major DAW on macOS, Windows and Linux. It works at a scale (thousands of grains,
minutes of buffer, multiple layers) where granular synthesis becomes an orchestra rather than an
effect.

**The gap it fills.** Small open-source granular plugins already ship CLAP/VST3/AU (dm-TimeWarp,
Orbit, DrumCloud). Commercial products are polished but closed, capped at tens to hundreds of
grains per voice, and limited to live buffers a few seconds long. **No open-source granular
instrument and effect exists with commercial depth and this scale.** Being open source also
means it cannot be discontinued out from under its users, as happened with Iris 2.

### 2. What it is

- **Two products, one engine.**
  - *Giga Grains* is an instrument that granulates samples and internal sources, played via MIDI/MPE.
  - *Giga Grains FX* is an audio effect that granulates its input and also accepts MIDI.
  - Both are thin shells around the same engine and share one state format.
- **Formats:** CLAP, VST3, AU v2, and a standalone app.
- **Platforms:** macOS universal (Apple Silicon and Intel), Windows, Linux.
- **Designed for, not in v1:** iOS AUv3 (self-build only; AGPL rules out App Store distribution)
  and LV2.
- **Possible later:** AAX. The SDK allows GPLv3-family projects, but it needs Avid/PACE signing.

### 3. Musical focus

All four are core. The engine is designed for all of them from day one.

1. **Evolving textures**: clouds, drones, freeze, smear. **This is the v1 anchor.**
2. **Playable instrument**: pitched, polyphonic, MIDI/MPE-expressive.
3. **Rhythmic / synced**: grain streams locked to host tempo, stutters, patterns.
4. **Extreme sound design**: massive density, huge stretch, spectral smear, synthetic grains.

### 4. Sound sources

Every source presents the same read interface to the grain engine:

- **Loaded samples**: audio files, dragged and dropped, possibly several at once.
- **Live input buffer**: a rolling record buffer with freeze, overdub and loop length.
- **Internal oscillators**: synthesized sources granulated in real time. No sample needed.
- **Resampling its own output**: recursive granulation and print-to-buffer.

### 5. What "giga" commits us to

- **Thousands of concurrent grains in a DAW plugin, at bounded CPU.** Stated per layer and per
  instance, with published figures (§16).
  - Competitors manage roughly 32 to 600 grains per voice, or about 1,000 per instance when
    polyphonic.
  - EmissionControl2, a standalone app rather than a plugin, reaches 2,048.
- **Minutes-long buffers.**
  - The distinctive part is **minutes-long *live* buffers inside a plugin**, saved with the
    session when held (frozen or looped) and managed with splices like Morphagene. Competitors' live buffers are seconds long.
  - Minutes-long samples and 100× to 1000× stretch come with this.
- **Many layers.** Four independent layers per instance (A to D). Each has its own source and
  settings, and they can be mixed or morphed.

### 6. Reference points and what we borrow

We credit the ideas we borrow. We do **not** use the names "Mutable Instruments", "Clouds" or
"Beads" for any part of the product.

| Reference | What we take |
|---|---|
| Mutable Instruments Clouds / Beads | Multi-function macros; quality modes as character; grain-stealing crossfades; wavetable fallback when there's no input |
| Make Noise Morphagene | Splice and organize workflow for long buffers |
| Hologram Microcosm | Curated algorithm variations; built-in looper |
| Output Portal / Arturia Efx Fragments | Macro morphing; capture/release; transient-synced capture |
| Borderlands / Emergence / Mangle | Live grain display over the waveform; per-parameter randomize with lock |
| Audio Damage Quanta / Ableton Granulator III | Per-note MPE on position and size; Classic/Loop/Cloud-style modes; depth |
| Minimal Audio Lucid / Bitwig Fragments | Scale-locked grains; grain repeats |

### 7. Who it's for

- Ambient and experimental producers, and sound designers (primary).
- Live performers: standalone use, MPE, macro performance.
- Modular users who want Clouds or Morphagene inside the computer.
- The open-source and Linux audio community.

### 8. Principles

When decisions conflict, these break the tie.

1. **Musical by default, deep on demand.** Every macro sounds good across its whole range.
   Randomness is *shaped*, never raw noise: distributions, scale quantize, tempo grids.
2. **One engine, many faces.** Texture, playable, synced and extreme modes are configurations of
   one grain engine, not four engines. The instrument and FX are thin shells around it.
3. **Uniform source interface.** File, live, oscillator and resample sources all look the same
   to grains.
4. **Real-time safety is non-negotiable.** The audio thread never allocates, locks or does I/O.
   Under overload the engine degrades gracefully and deterministically (§14: a lower
   interpolation tier first, then dropped new grains, then stolen grains with fades) and never
   drops out.
5. **Scale is a design input, not a later optimization.** The SIMD grain pool exists from day
   one. Thousands of grains is the baseline benchmark.
6. **Deterministic.** Randomness is seeded, so the same session renders identically every time.
   That makes automated regression testing possible.
7. **The canvas tells the truth.** It draws the grains the engine is actually playing, not a
   decorative animation.
8. **Portable sessions.** A project opened on another machine sounds identical. Samples are
   embedded or recoverable, and the state format is versioned with migrations.
9. **Machines verify, ears judge.** CI proves correctness, performance and real-time safety, so
   human listening is spent only on musical judgment.

### 9. Non-goals (for now)

- Multisample zone mapping. This is not a sampler.
- Song or arrangement sequencing.
- A content store, user accounts, telemetry or copy protection.
- VST2.
- GPU audio processing.
- App Store distribution.
- Spatial or immersive output formats.

AAX is listed as *possible later* (§2), not as a non-goal.

### 10. Success criteria for v1.0

1. The director reaches for it in their own music.
2. Texture mode holds up against the reference instruments in blind listening.
3. The grain-count and CPU figures published in §16 are measured on the reference hardware and
   reproducible.
4. It passes clap-validator, pluginval and auval, and is stable in the first-class hosts (§11).
5. Renders are reproducible: bit-exact on the same platform.

### 11. Hosts

- **First-class: Ableton Live 12 and Bitwig Studio.** Manually tested at every milestone. Bugs
  there block a release.
- **Best effort, plus automated validators:** Logic Pro, REAPER, FL Studio (a CLAP host),
  Renoise, Fender Studio Pro, Cubase.

### 12. License and content

- **All project code is AGPL-3.0-or-later.** Every dependency must be AGPLv3-compatible.
  - Compatible: AGPL-3, GPL-3 (combined under GPLv3 §13; the ASIO SDK's GPLv3 option, §23 #56),
    MIT, BSD, Apache-2.0, zlib, ISC, BSL-1.0, Unlicense, the Unicode data license (utf8proc,
    §23 #55).
  - **Not allowed:** GPLv2-only code, and code with no license at all.
  - JUCE is used under its AGPLv3 option.
- **Factory content:**
  - a small curated set of **CC0** samples (about 20 to 50 MB) chosen because they granulate well;
  - presets: about 100 curated ones across the four focuses by v1.0;
  - kept out of git history (via LFS or a release asset).

### 13. How it's built

Development is **Claude-led**: Claude writes most of the code, and the director steers, listens
and reviews. Two consequences:

- Automated verification (§19) is a first-class part of the product, not overhead.
- Every milestone ends with a listening gate: rendered A/B files plus a short checklist for the
  director.

---

## Part II — Initial design direction

*Expected to evolve. Record every change in the [decision log](#23-decision-log).*

### 14. Engine architecture

```
 Sources ─────────► ┌──────────────── Layer (×4: A–D) ────────────────┐
  sample (RAM)      │ Source ─► Scheduler ─► Grain pool ─► Kernels ─► │ ─► buses, layer FX, diffuser
  live ring         │           (when/what)  (SoA, fixed)  (SIMD)     │
  oscillator        └─────────────────────────────────────────────────┘
  resample tap ◄──────────────────────────────────────────────┐
                                                               │
 Layers A–D ─► Mix / XY morph ─► Global FX (delay · reverb · limiter) ───────────┴─► Out
          ▲
 Modulation: macros · LFOs · envelopes · random/S&H · step · MIDI/MPE · env follower · transport
```

**Units**

- **Source.** Owns or generates audio and exposes a batched `read(position, rate)` interface.
  The kinds are §4's four, Sample, Live, Oscillator and Resample, plus Spectral, a smear of a
  layer's sample or ring (M2 sub-project 4). Each layer picks its kind with its Source parameter
  (since M2 sub-project 1; The Source kind, below).
- **Scheduler.** One per grain stream. It decides *when* grains start and *with what* parameters.
  - Modes: **Cloud** (asynchronous; built in M1), **Sync** (tempo grid; M4), **Voice** (MIDI/MPE
    notes spawn streams; M3), **Pitch-sync** (grain rate tied to pitch, for pulsar and formant
    sounds; M3). All four ship before 1.0 (§20).
  - Clocks: free-running Hz (0.01 Hz up to the sample rate; the plugins' Density stops at
    400 Hz), tempo divisions, MIDI pitch-sync, external or transient trigger.
  - A distribution control runs continuously from 0 (synchronous) to 1 (random, Poisson).
  - Onsets follow a recurrence driven by the counter RNG, with sub-sample fractional timing
    (Cloud core rules, below).
- **Grain pool.** Structure-of-arrays storage, preallocated in `activate()`.
  - Defaults to 4,096 grains (`EngineConfig::grainCapacity`; both plugins use the default).
    **Each layer owns one** (since M2 sub-project 1; §23 #65): an instance holds four, 16,384
    grains in all, so no render task shares a mutable pool, and layer A admits exactly as M1's
    one pool did. **As built (sub-project 3; §23 #66):** an engine's ceiling is
    `kMaxGrainCapacity` = 16,384 grains (validated at construction; the pool's storage, 2 × capacity
    rows, is about 150 bytes a row, 4.9 MB at the ceiling), and the plugins keep 4,096 per layer
    (`ProcessorConfig::grainCapacity`): M2's ranges reach 800 grains a layer and the default
    Budget is 4,096 grains, so M3's voices raise it where they need to. A full pool degrades as a
    spent budget does (The deterministic CPU budget, below), not by a bare drop. An instance's
    memory is itemized once, under Layers and the chain (below); the event buffers (4 layers ×
    4 × 4,096 × 96 bytes) are 6.3 MB of it.
  - Per-layer and per-voice quotas.
  - Dense index arrays with **stable** compaction that preserves spawn order, so per-sample
    summation order (and therefore output) is independent of host block size.
- **Kernels.** "Grain-major" SIMD kernels, vectorized over time within each grain. Templated on
  channel count × interpolation tier × source kind.
  - Tiers: **Eco** = linear, **Normal** = 4-point Hermite (the default), **HQ** = 12- to 16-tap
    windowed sinc.
  - The tier is latched per grain, at its onset. Until M2 only the engine and `gg-render` reach
    Eco; both plugins stay Normal (§23 #43). Since M2 sub-project 3 every layer has a **Quality**
    parameter (Eco, Normal, HQ; default Normal), and only the overload ladder lowers a grain below
    it (§23 #66).
  - **HQ, as built** (sub-project 3): a 16-tap Kaiser-windowed sinc (β 8.0; the director's D3,
    2026-10-30), its table of 512 points per unit read with linear interpolation, divided by the
    sum of the taps it used so DC is exact. At stretch 1 the kernel is within 0.0007 dB up to 0.3
    of the source's sampling rate and at least 81.7 dB down from 0.7. For an upward residual
    rate `r` the kernel is stretched by `s = min(1, 1/r)`, latched per grain, so its cutoff
    follows the rate; at s = 0.5 the 16 taps cover half the window and the stopband is only −37 dB
    (ruling ES-T1a). The scalar reference defines the bits and the SIMD kernels match it by
    `memcmp`, at every source end, rate, mip level and stretch. The live ring's guard widens to
    15 frames for it. On the M5 Max an HQ grain costs about 6.8 Normal ones.
  - **Wider x86 SIMD, as built and kept** (sub-project 3; §22, §23 #66): the sample and live
    kernels exist 8 lanes wide for AVX2 beside the 4-wide ones, in a translation unit whose
    functions each carry a `target("avx2")` attribute (not a file-level flag, so no shared header
    code is compiled with AVX2). The table is chosen once, at the first `Engine`, from CPUID
    (OSXSAVE, AVX2 and the YMM state), falling back to the 4-wide table; `GG_FORCE_SSE`, set and
    not empty, forces the fallback; `EngineConfig::kernels` (`Auto` or `Sse`) chooses per engine.
    Both tables render the same bits (no FMA, no contraction), so what varies is speed alone,
    as it already does between a NEON and an SSE build.
  - Run counts are computed per block, so there are no per-sample branches.
- **Grain buses / layer FX.** Partikkel-style channel masks route grains to a few buses per
  layer, each with its own FX. True per-grain filters come later, behind a budget weight.
  - **As built** (M2 sub-project 4; §23 #69): two buses per layer, a filter each (Extremes and
    the FX chain, below). §22's question, whether buses are enough for v1 or per-grain filters
    are needed sooner, is answered there with a measurement.
- **Diffuser.** One per layer, on its output: a short allpass chain that blurs the grains into
  each other, part of the granular sound rather than a room. It is a per-layer unit, so it sits
  in §17's per-layer "Space" panel (since M1 sub-project 4; §23 #50).
- **Mix and global FX.** Four layers feed a mix with XY morph, then delay, reverb and a limiter.
  - As built (M2 sub-project 1; §23 #65), the layers' weighted sum feeds M1's reverb and mix
    (Layers and the chain, below). The global delay and limiter (M2 sub-project 4; §23 #69)
    are built, each the identity at rest, and sit where Extremes and the FX chain (below)
    draws them.
  - The resample tap reads the instance output 256 samples late, the quantum of the Feedback
    pieces and never the host block, so block-size invariance holds. It adds no latency; its
    history is two stores a sample whatever the kinds (ruling SP-T12ac, §23 #68; M2 program
    design §7.2, §23 #64).
    - **As built** (M2 sub-project 2, 2026-10-30; §23 #68): the tap hears `out` as the host
      hears it, the mix with the dry signal and the reverb's return, and from sub-project 4 the
      limiter (the director's D4, option 1), so in the FX a Resample layer re-grains the dry
      input too and the reverb's tail recurses. The `Processor` keeps the last 256 output
      samples per channel by absolute time (`t mod 256`), written after each piece's output
      whatever the kinds (ruling SP-T12ac, 2026-10-30: two stores a sample, so the tap costs
      that much while no layer is Resample) and silence before the first sample after a reset.
      A Resample layer's ring input for sample t is `tap[t − 256]` in place of the instance
      input; one output channel feeds both sides. A switch to or from Resample crossfades the
      ring's input between the two over 10 ms from its boundary (ruling SP-T12ab), so the frames
      written at a switch never jump and the grains reading across them never click.
  - It also has a DC blocker and a soft limiter.

**Layers and the chain** (M2 sub-project 1, built 2026-10-30; §23 #65). Four layers, A to D,
each a complete grain engine with M1's whole chain, summed in that order. Each owns a
`gg::Engine` (`EngineConfig::layer` is its index, the `layer` of every RNG key it draws, so layer
A's draws are M1's), its pool of 4,096 grains, a 16 s live ring (`kLiveSeconds`, in both
products), its two sample slots, a diffuser, a Level (layer A's is M1's `Level`, B to D's are
`Level B` to `Level D`), Feedback with its 256-sample limited history, the soft limiter, a peak
strip and a fixed grain-event buffer. The `Processor` (`gg-core`) is the instance: it owns the
four, the morph, the mod matrix, the held set, Space and the mix. Everything is allocated at
construction, whatever a layer's kind and whether it is on, since a kind switch or an on/off is
a parameter change on the audio thread. **Memory at 48 kHz, computed from these components'
sizes (not measured; the M2 close, §23 #89), is about 80 MB per instance at the defaults:**
- **the rings, counted against the memory cap:** four 16 s stereo rings with their three mip
  levels, 11.5 MB each (Memory management, below), about 46 MB;
- **the fixed costs, documented, not counted:** four pools about 4.9 MB (2 × 4,096 rows of about
  150 bytes each), the reverb 14.5 MB, the event buffers about 6.3 MB, and sub-project 4's
  8.4 MB (the Spectral Smear's rings and STFT buffers, four layers' worth, and the delay line's
  0.8 MB; Extremes and the FX chain, below), about 34 MB in all.
- **not in either list:** each layer's `LivePeaks` is sized for a 300 s ring whatever the ring's
  length (the editor's peak strip, one float per 64 frames), about 0.9 MB a layer at 48 kHz and
  scaling with the rate, so about 3.6 MB an instance, outside the memory cap.

A loaded sample, with its interleaved copy and levels, is counted against the cap on top. Since
M2 sub-project 2 (§23 #68) a ring's length is its layer's Ring Length, 16 s by default, and a
ring of another length is allocated on the main thread and adopted through the layer's exchange
(Live ring buffer, below); the rings and samples are bounded by the per-instance memory cap
(Memory management, below).

Both products render in pieces of at most 256 samples (`kFeedbackDelay`; M1's instrument rendered
whole blocks, and block-size invariance makes the piece size change no bit). Per piece:

```
per layer L (A..D), one task:
   ring_L[t] = sanitize(x_L[t]) + fb_L(t) · limited_L[t − 256] + o_L(t) · ring_L[R − P]
              (x_L: the input, or tap[t − 256] on a Resample layer; o_L: its Overdub; P its
               period; both since M2 sub-project 2, Live ring buffer, below)
   w'_L      = δ_L > 0 ? w_L + δ_L·(diffuse_L(w_L) − w_L) : w_L   (w_L: its grains)
   limited_L = softLimit(w'_L · g_L)                                (g_L: its Level's gain)
   wet_L     = limited_L · weight_L(t)                              (its morph weight)
instance, after the tasks join:
   wet = wet_A ⊕ wet_B ⊕ wet_C ⊕ wet_D   (the first rendering layer assigns, each later one
                                          adds; +0 when none renders)
   then Space and the mix on wet, as M1's (Space, below)
```

w_L sums the layer's grains in spawn order.

- **The morph law** (the director's D1, option 1, 2026-10-29, as amended by ruling LS-T10a,
  2026-10-30, and for carry-over row 301, 2026-10-31, §23 #69). Morph X and Morph Y are
  globals, 0–1, 50% by default, so the pad rests at the center. With x and y their ramped values
  clamped to [0.001, 0.999], the corner weights `b_A = (1 − x)(1 − y)`, `b_B = x(1 − y)`,
  `b_C = (1 − x)y` and `b_D = xy` (with Y up: A bottom left, B bottom right, C top left, D top
  right; §17),
  `r_L` each layer's on/off ramp (0 for a layer not rendering), `S` a set of on layers and
  `s = Σ_{K∈S} b_K`:

  ```
  at rest:  W_S,L = b_L / s                         (D1's sum-normalized vector law)
            W_S,L = (b_L + (ε − s) / |S|) / ε       (a diagonal pair or a triple, s < ε = 0.35)
  ramping:  weight_L = Σ_S P_S·W_S,L / Σ_S P_S × min(1, Σ_K r_K),
            P_S = Π_{K∈S} r_K · Π_{K∉S} (1 − r_K)  (the ramping layers; non-empty S)
  ```

  - At rest it is the sum-normalized vector law: the amplitude sum is 1 wherever the pad is,
    every on layer weighs 1/n at the center, and a corner's layer sounds alone at its corner. A
    second layer switched on lowers the first by 6 dB at the center; the Levels restore the
    balance.
  - On the clamped pad that law is continuous, but steep where a diagonal pair's or a triple's
    corner weights all near 0, near the corner none of them holds (up to about 200 per pad
    unit). There the floor gives each on layer an equal share of the shortfall: it binds on
    17.1% of the pad for a diagonal pair and 7.1% for a triple. A lone layer, an adjacent pair
    (A and B weigh `1 − x`, whatever y is) and four layers never floor, so their resting weights
    are D1's bit for bit on the whole pad.
  - While layers ramp, the weights interpolate multilinearly between the on-sets the ramps span,
    so each moves at the ramps' pace. At the center the crossfade is linear in the ramp (A on, B
    ramping in: `1 − r/2`, `r/2`); weighting the sets by size would keep the first law's
    hyperbolic shape but double the worst handover step. Dividing `r_L·b_L` by `Σ r_K·b_K`, as
    first built, let a ramp near 0 on a dominant corner outweigh the rest and handed over within
    a few samples (row 301).
  - The largest per-sample weight step, at 48 and 44.1 kHz, on the control grid: a resting pad
    crossing in both axes at full speed, 0.0073 and 0.0079; a handover with the pad still,
    0.0062 and 0.0068; both at once, 0.0105 and 0.0116. ε = 0.35: at 0.25 the pad alone passes
    0.01 at 44.1 kHz (0.0102) and both at once reach 0.0136; a larger ε floors more of D1's pad
    (0.45: 33.6% and 12.2%) and none up to 0.45 brings both at once within 0.01 at 44.1 kHz (the
    layers spec's §6.3 has the table).
  - The factor `min(1, Σ_K r_K)` (LS-T10a, `1235944`) makes the amplitude sum follow the ramps
    while less than one whole layer's worth is on, so the last layer switched off fades over its
    ramp instead of cutting when it reaches 0, and the first one switched on from all off fades
    in.
  - One layer rendering with its ramp at 1 weighs exactly 1.0f wherever the pad is
    (`b / b`, one set with `P = 1`), and the sum assigns its first layer rather than adding it
    to +0, so layer A alone renders M1's chain bit for bit, the sign of zero included.
  - Plain float per sample, no libm, computed serially into a per-piece buffer before the tasks
    run. No weight is −0: the ramps and corner weights are products of non-negative values.
- **On and off** (`Layer A On` to `Layer D On`, stepped; A on, B to D off by default). A switch
  ramps `r_L` over 10 ms; a layer renders while its ramp's target or value isn't 0.
  - When the ramp reaches 0 the layer is off: its engine is disabled (the enable gate,
    `Engine::setEnabled`: its pool cleared, so nothing pops back in mid-grain later, and nothing
    spawned), its diffuser and Feedback history are cleared, it isn't rendered or summed, and its
    task isn't dispatched. Only its ring is written and its clock advanced, so a Live layer
    switched on after a minute reads the last 16 s. The piece is cut at the sample its ramp
    reaches 0, so where a layer stops doesn't depend on block size.
  - Switched on, its engine is enabled at that boundary with a forced onset, as a note-on forces
    one, so it speaks at once rather than waiting up to 4 s at Density's floor; its ramp rises
    from 0. Switched off and on again within the ramp, the ramp turns from its current value and
    the engine is never disabled.
- **Notes act on every layer.** The held set is the instance's: every layer's grains pick their
  keys from it, the FX's held notes freeze every layer's ring, and the instrument's note gate
  gates every layer. Per-layer or per-channel note routing is M3's, with the Voice scheduler.
- **The instrument's sidechain input** (the director's D2, option 1, 2026-10-29): a stereo CLAP
  input port, "Sidechain In", not main and with no in-place pair, which clap-wrapper makes an aux
  bus in VST3 and an input bus on the AU (each of the three AU identities has one stereo input,
  `8824c26`). It feeds every layer's ring, so an instrument layer set to Live hears it; the
  instrument has no dry path. The FX's main input feeds every layer's ring and its dry path.
  D2's fallback (dropping the port) was not needed: CI's macOS leg (run on `1aa7114`) passed
  auval on all three identities with the instrument's input bus, and pluginval and Steinberg's
  validator passed too.

**The Source kind** (M2 sub-project 1; §23 #64, #65). `Source` is a stepped per-layer parameter
(index `0x..0C`) whose enumeration is complete from the start, so its range never changes once
frozen: **0 Sample, 1 Live, 2 Resample, 3 Spectral, 4 Oscillator.** Its default differs by
product, as Position's does: Live in the FX, Sample in the instrument.
- **Sample and Live are built, in both products.** An FX layer set to Sample reads the sample
  `gg-render` gives it, or its own loader's in the plugin (since M2 sub-project 2, four loaders
  per instance in both products, one per layer; §18); an instrument layer set to Live reads its
  ring.
- **Resample is built, in both products** (M2 sub-project 2, 2026-10-30; §23 #68). A Resample
  layer is a Live-mode engine whose ring the tap feeds instead of the input (Mix and global FX,
  above): `builtKind(Resample, mode)` is Live in both products, the kernels are the live kernels,
  and Position, Spray, Freeze, Feedback, Loop, Overdub and splices work on it unchanged. The text
  rule (ruling FR-6) and the text rescan treat it as Live. So the instrument, whose layers
  default to Sample, has a writable buffer and Feedback.
- **Spectral is built, in both products** (M2 sub-project 4; §23 #69): a layer set to Spectral
  smears its material (Extremes and the FX chain, below).
- **The unbuilt kind falls back to the product's default kind:** Oscillator renders as Live in
  the FX and as Sample in the instrument, the parameter keeping its value, so a state saved with
  it plays as Oscillator once that kind lands. The fallback is one function,
  `builtKind(kind, mode)` (`gg/core/ParamMap.h`): sub-project 2's edit made it return Live for
  Resample, and sub-project 4's Spectral for Spectral.
- **Position, Spray and Feedback follow the layer's built kind, not the product.** A Spectral
  layer's Position follows its material (below) and its Feedback is 0, as on a Sample layer. A
  Live layer
  reads seconds behind the write head, one-sided (ruling M1-k); a Sample layer reads the fraction
  of its sample with Spray bipolar in seconds (Notes, below); Feedback writes a Live layer's ring
  and is 0 on a Sample layer. The plugin asks the host to rescan the parameter text
  (`CLAP_PARAM_RESCAN_TEXT`, on the main thread) when a layer's kind changes, so Position shows
  seconds or percent; no range changes.
- **A kind switch doesn't click.** It applies at its boundary (`Engine::setSourceMode`): the
  scheduler draws for the new kind from there, and the grains still sounding finish on the
  source they started on, as a sample swap already let them. Each grain's pool row records its
  sample slot or the ring (`Engine::kLiveSlot`), and the renderer dispatches per grain in spawn
  order, so the summation order is unchanged. The pool is no longer cleared on a switch
  (carry-over rows 118 and 119, closed by `263696d`); removing the sample entirely stays the one
  hard cut, by design.
- **The level accessor.** `PreparedSource` and `LiveSource` expose `levelCount()`, 1 for now, and
  `level(n)`, level 0 for any `n` at or past it; the kernels read level 0's view, so nothing
  moved. Sub-project 3 fills the pyramid and picks a level per grain behind it, and sub-project 2
  builds the ring's levels.

**Per-grain spreads** (M2 sub-project 1; the director's D4 and D5, 2026-10-29; §23 #65). Two
per-layer parameters, each uniform bipolar, 0 by default:
- **Pitch Spread** (`0x..0D`, 0–24 semitones; D4, option 1): a grain plays
  `clamp(Pitch + (key − 60) + spread × bipolar(u), −48, 48)` semitones, with `u` keyed by
  (seed, layer, stream, `Field::PitchSpread` = 5, k).
- **Size Spread** (`0x..0E`, 0–2 octaves; D5, option 2): a grain lasts
  `exp2Det(clamp(log2 d + spread × bipolar(u), log2 0.005, log2 2))` seconds, with d the layer's
  Size in seconds (its log through `logDet`) and `u` keyed with `Field::SizeSpread` = 6. The
  latched gain normalization keeps the layer's Size, not the grain's, so a spread doesn't pump
  the level.
- At 0 neither draws, and each takes M1's path bit for bit. The fields were appended to
  `rng::Field`, so no existing draw changed. The grain-event line's `rate` and `dur` carry both.
  The scheduler accepts up to 48 semitones and 4 octaves; the parameters reach 24 and 2.
- Since M2 sub-project 6 the canvas's y axis is pitch, so Pitch Spread shows there (§17).

**The mod matrix** (M2 sub-project 1; the director's D3, option 3, 2026-10-29; the program
design's decision 7; §23 #64, #65). `gg/core/ModMatrix.h` and `gg/core/ModSources.h`.
- **48 slots**, each `{source, sourceLayer, target, targetLayer, depth, curve}`, saved in the
  patch (`PTCH`'s `<mod>`, §18), never as parameters, and evaluated in slot order. A fresh
  instance, and a migrated M1 state, fill slots 1 to 12 with the factory routes (below); any
  slot may be edited or cleared (from `gg-render`, a state and, since M2 sub-project 6, the Mod
  view; §17, §18).
- **The sources,** each a value in [0, 1] on the 32-sample grid, each a function of the sample
  clock, the parameter history and the host's events, never of time, CPU or thread:

  | Source | Scope | Value | Determinism |
  |---|---|---|---|
  | Macro 1–8 | global | the macro's effective value `clamp(base + mod)` | as M1 |
  | LFO 1, LFO 2 | per layer | Sine, Triangle, Saw Up, Saw Down or Square at the phase, each 0–1 centered on 0.5; the sine is `0.5 + 0.5·sineDet(p)`, a quarter-wave polynomial with no libm | the phase accumulates `rate × 32 / sr` per cell from 0 at reset, so a rate change bends it but never jumps it |
  | Env 1, Env 2 | per layer | a linear ADSR gated by the held set: on while any key is held | stepped per cell in plain arithmetic, its level a double; each time at least one cell |
  | Random 1, Random 2 | per layer | a new target every `1/rate` s, glided toward with τ = Smooth / rate (Smooth 0 jumps) | target k is `uniform01(hash(seed, layer, 0, Field::ModRandom1 = 7 or ModRandom2 = 8, k))`, k counting periods from reset |
  | Step | per layer | Step 1–16's values at Step Rate, Step Length of them cycling | the step counter, from the clock |
  | Mod Wheel, Aftertouch, Pitch Bend | global | the last MIDI CC 1, channel pressure and pitch bend (center 0.5) | events on the grid, as notes are |
  | Velocity, Key | global | the last note-on's velocity and key / 127, CLAP's or MIDI's | ditto |
  | Env Follower | global | a one-pole on the input's peak over the previous cell, 5 ms attack and 200 ms release, at most 1 | coefficients fixed at construction (`exp2Det`); the first cell's input counts as silence |
  | Transport Beat, Bar, Playing | global | the host's beat phase, bar phase and playing flag, advanced per cell while it plays and held otherwise; 0 before any | the host's transport alone; `gg-render` gives a steady one or none |

- **The per-layer sources' controls** are layer parameters, `0x..0F` to `0x..30` (§18). A
  per-layer source's `sourceLayer` is A to D or Same, the target's layer (A's for a global
  target), the default.
- **Targets:** any parameter but the eight macros, the modulators' own controls and Ring Length,
  so one pass in slot order is provably deterministic (`isValidTarget` admits every layer
  parameter but the modulators' controls, `isModulatorControl`, and Ring Length (ruling SP-T12c,
  §23 #68), so the parameters of later sub-projects, Quality among them, are targets; ruling
  PM-1, §23 #66). A per-layer kind is named by layer A's slot (Level
  and Layer A On for the per-layer globals) and reaches one layer or every layer (`targetLayer`
  A to D or All; "every layer" for Level means the four Levels); Mix, the reverb's, Morph X and
  Y take no layer.
- **The curves,** M1's macro expressions so the recipes are exact: `Unipolar` u, `Bipolar`
  2u − 1, `BipolarPositive` max(2u − 1, 0), `UnipolarSquared` u², `RampThreeQuarters`
  min(u / 0.75, 1), `TopQuarter` smoothstep(0.75, 1, u). A route adds `depth × curve(source)` in
  the target's CLAP value domain, in slot order, and the effective value stays
  `clamp(base + mod + Σ)` in one clamp.
- **The twelve factory routes,** slots 1 to 12 in this order, each on every layer but Mix, at
  M1's depths:

  | Slot | Source | Target | Depth | Curve |
  |---|---|---|---|---|
  | 1 | Macro 1 | Size | +3 | Bipolar |
  | 2 | Macro 1 | Density | −1.5 | Bipolar |
  | 3 | Macro 2 | Density | +4 | Bipolar |
  | 4 | Macro 2 | Distribution | +0.25 | BipolarPositive |
  | 5 | Macro 3 | Pitch | +24 | Bipolar |
  | 6 | Macro 4 | Spray | +0.5 | Bipolar |
  | 7 | Macro 4 | Stereo | +0.3 | BipolarPositive |
  | 8 | Macro 5 | Texture | +1 | RampThreeQuarters |
  | 9 | Macro 5 | Diffusion | +1 | TopQuarter |
  | 10 | Macro 6 | Freeze | +1 | Unipolar |
  | 11 | Macro 7 | Feedback | +0.9 | UnipolarSquared |
  | 12 (FX) | Macro 8 | Mix | +0.5 | Bipolar |
  | 12 (instrument) | Macro 8 | Level, every layer | +12 | Bipolar |

  They give M1's `macroOffsets` bit for bit (a test compares them over 10,000 random macro
  vectors in both products, against a copy of M1's function), and at rest each is exactly +0.0f.
- **The sources' clock.** Every source steps exactly once per grid boundary b, in order, as
  rendering reaches b: after b's events are committed, before b's effective values are computed,
  however the host splits blocks. A source's value after stepping at b is the cell
  [b, b + 32)'s. The routes' sum reaches the engines at the boundary, as a parameter event does,
  and a ramped target ramps from there, so a fast LFO on a ramped target lags by the 10 ms ramp
  (M1's semantics).
- **Per-cell application.** While some route has a source that moves with time alone (the
  per-layer sources, the Env Follower, Beat and Bar), a non-zero depth and a valid target, every
  boundary applies, as an empty group where no event pends, so the values follow the sources
  cell by cell. Otherwise only the boundaries with events apply, as in M1, while the sources keep
  time. No pre-M2 golden has such a route, so their pieces and bits are M1's.
- **Resets.** Construction and `reset()` reset every source, the transport to none given (ruling
  LS-T12a), so a reset `Processor` renders as a fresh one. A state load, a `setBase` of a macro
  and a change of the routes jump the ramped values they move (ruling PU-T2a); Macro 6 is still
  seen as Freeze for arming, per layer.

**Parameter model.** The internal model is a `GrainEvent` derived from Csound's *partikkel*:
- onset (int64 + fraction) and duration;
- envelope: shape, skew, sustain fraction;
- amplitude and gain mask;
- bus/channel mask;
- 2 to 4 source references, each with a mix weight and a 64-bit position;
- rate start/end with a sweep curve;
- FM index;
- random drop.

Every field is **base + modulation + per-grain spread**. Spread has a selectable distribution and
optional scale or grid quantize. This is how "shaped randomness" (§8.1) is implemented. As built,
position (Spray), pan (Stereo) and onset (Distribution) have their spreads, and since M2
sub-project 1 pitch and size do too (Pitch Spread and Size Spread, uniform; Per-grain spreads,
below). The rest of the model, Texture's spread and the distribution selector among them, is a
§22 question for after M2 (the director's D5; §23 #65).

**Two-rate modulation.**
- Continuous controls (layer filters, mix) update on the 32-sample control grid (Cloud core
  rules, below). Mix, each layer's Level, Diffusion and Feedback, Reverb Amount, Dry to Reverb,
  and since M2 sub-project 1 Morph X, Morph Y and each layer's on/off ramp, are also smoothed per
  sample.
- The modulation sources step once per grid cell, and the mod matrix's routes reach the engines
  at the grid boundaries (The mod matrix, below).
- Grain parameters are **snapshotted when each grain starts**, so each grain gets its own value.

**Macros** (M1 sub-project 5, §23 #58; the mod matrix's since M2 sub-project 1, §23 #65). The
eight macros are relative: each is a modulation source that offsets a few targets from the base
values the preset sets. Each is a CLAP parameter, 0–1, saved like any other. M1's fixed factory
recipes are now the mod matrix's twelve factory routes (The mod matrix, below), each targeting
every layer, with M1's depths and M1's float expressions, so they give M1's offsets bit for bit;
a user may edit or clear them like any route.
- **The effective value** of every parameter is `clamp(base + mod + Σ routes)`, in one clamp,
  never two. Each route adds the target's offset in its own CLAP value domain (octaves for Size
  and Density, semitones for Pitch, dB for Level, the 0–1 domain for the rest), from the macro's
  own effective value `v = clamp(base + mod)`; no route targets a macro. With `b = 2v − 1`,
  `b⁺ = max(b, 0)` and `u = v` (M1's `macroOffsets`; since M2 sub-project 1 `factoryRoutes`, in
  `gg/core/ModMatrix.h`):

  | Macro | Polarity | Offsets |
  |---|---|---|
  | 1 Size | bipolar | Size `+3b`; Density `−1.5b` |
  | 2 Density | bipolar | Density `+4b`; Distribution `+0.25·b⁺` |
  | 3 Pitch | bipolar | Pitch `+24b` |
  | 4 Spray | bipolar | Spray `+0.5b`; Stereo `+0.3·b⁺` |
  | 5 Texture | unipolar | Texture `+min(u / 0.75, 1)`; Diffusion `+smoothstep(0.75, 1, u)`, with `smoothstep(e0, e1, x) = s²(3 − 2s)`, `s = clamp((x − e0)/(e1 − e0), 0, 1)` |
  | 6 Freeze | unipolar, stepped | Freeze `+u` (effective Freeze is on above 0.5) |
  | 7 Feedback | unipolar | Feedback `+0.9u²`, in both products since M2 sub-project 1 (M1's instrument had Stereo `+0.5b`, bipolar, here; an M1 instrument state folds it into Stereo's base, §18) |
  | 8 Mix (FX) | bipolar | Mix `+0.5b` |
  | 8 Level (instrument) | bipolar | Level `+12b` dB |

  A target's offsets add in route order, which is macro order. Every depth is the spec's
  starting value, kept as shipped at listening gate #1 (§23 #60) and kept in the factory routes
  by M2 sub-project 1; revisiting them is the director's, at M2's gate.
- **Polarity keeps a resting preset bit-identical.** A bipolar macro rests at 0.5 and a unipolar
  one at 0, and there every offset is exactly +0.0f (`2 × 0.5f − 1` is exactly 0), so
  `base + mod + 0` is `base + mod` bit for bit (a −0 sum becomes +0).
- **On the grid.** A macro event applies at the next 32-sample boundary like any other
  parameter's (Cloud core rules, below); a target that ramps (Mix, Level, Diffusion, Feedback)
  ramps from the macro's boundary. A `setBase` of a macro (params.flush, a state load) jumps the
  ramps of the targets it moves, as a `setBase` of the target does, and Macro 6 is seen as
  Freeze is for arming (ruling PU-T2a).
- **The curves are libm-free:** plain float arithmetic, bit-identical on every platform.

**Texture** (M1 sub-project 5; §23 #58): one layer parameter, 0–1, default 0, that drives the
envelope fields of the parameter model above (shape, skew, sustain fraction) along one fixed
family (`gg/grain/Envelope.h`). With τ the grain's Texture and d its duration in samples,
`aMin = min(0.5, max(0.05, 16 / d))` is the gated anchor's attack and release, each at least 16
samples and 5% of the grain, never more than half of it:
- τ ≤ 0.5: attack `a = 0.5 + 0.4τ`, release `r = 0.5 − 0.4τ`, no sustain (at 0.5 the peak is
  at 70% of the grain);
- τ > 0.5: with `s = 2τ − 1`, `a = (1 − s) × 0.7 + s × aMin` and `r = (1 − s) × 0.3 + s × aMin`
  (the lerp form, exact at both anchors; ruling PU-T3a), and a flat sustain `1 − a − r` between;
- so `a + r ≤ 1` always. The 16-sample floor binds only the gated anchor (ruling PU-T13a): for
  τ ≤ 0.5 the attack and release scale with the grain, as M0's Hann always has (an 8-sample
  attack on a 16-sample grain at Texture 0, kept bit for bit), so a 16-sample grain is a plain
  Hann only at Texture 0 and 1 (`aMin` is clamped to 0.5 there).

The window at phase φ ∈ [0, 1] is `hann(warp(φ))`, with
`warp(φ) = φ < a ? φ × (0.5/a) : (φ > 1 − r ? 1 − (1 − φ) × (0.5/r) : 0.5)` and `hann` the
existing Hann table.
- **Texture 0 is bit-identical to the plain Hann:** `a = r = 0.5` exactly, both scales are
  exactly 1, and `1 − (1 − φ)` is exact for φ ∈ [0.5, 1] (Sterbenz), so `warp(φ) == φ`. Every
  golden's WAV kept its SHA-256 (`f54cb85`).
- **Latched per grain,** like its gain: at spawn the pool stores four doubles, the attack's end
  `a`, the release's start `1 − r` and the two scales `0.5/a` and `0.5/r`. The one `warpPhase`
  serves both scalar references and the SIMD interior, so they stay bit-identical by
  construction; the SIMD interior skips the warp for a plain-Hann grain. Plain IEEE arithmetic,
  no libm.
- **The grain-event log** carries each grain's Texture as `"tex"`, after `"dur"`; since M2
  sub-project 3 `"tier"` and `"lvl"` follow it (§19).
- The gain normalization is unchanged: Texture changes only the envelope.

**Feedback** (M1 sub-project 5, §23 #58; per layer, in both products, since M2 sub-project 1,
§23 #65): one layer parameter, 0 to `kFeedbackMax` = 0.95, default 0, that regenerates the
layer's wet signal into its live ring. Per sample t and channel, the ring's input becomes

```
ring input[t] = finite(in[t]) + fb(t) · limited[t − D]        (limited = 0 before the first sample)
```

which the ring then stores as it stores any input (non-finite as 0, clamped to ±2^20). `limited`
is the layer's wet signal after its Level and soft limiter, before its morph weight and the
dry/wet mix, so `|limited| < 1`; `fb(t)` is the layer's Feedback, ramped per sample over 10 ms
like Mix.
- **D = 256 = M,** the ring's head margin (`kFeedbackDelay`, static-asserted equal to
  `LiveSource::kHeadMargin`). The admission clamp keeps every grain at least M frames behind the
  head, so the `Processor` renders in pieces of at most 256 samples (it, not the engine, owns
  the pieces; both products since M2 sub-project 1, the FX alone before): each piece's feedback
  was rendered before the engine writes the piece into the ring, so no grain reads a frame whose
  feedback isn't there yet. The last 256 limited
  samples are kept by absolute time (`t mod 256`), so the output is bit-identical at any host
  block size and any maxBlockSize, 1 included.
- **Feedback 0 is today's path:** a piece whose ramp is 0 throughout hands the engine its input
  pointers, and a sample where the ramp is 0 writes the input alone, so where a piece starts never
  changes the ring, the sign of zero included.
- **Freeze wins.** While frozen the ring's write gain is 0 (the 20 ms ramp), so neither input nor
  feedback is written, and the frozen buffer holds bit for bit.
- **Bounded:** `|limited| < 1` and `fb ≤ 0.95`. 60 s at Feedback 0.95, Density 128 Hz, Size 1 s,
  Level +12 dB and Stereo 100%, over 2 s of loud noise then silence, stays finite and below 1,
  and its last 10 s are no louder than seconds 16–26 within 1 dB (the `[slow]` test).
- **Feedback follows the layer's kind** (since M2 sub-project 1; §23 #65): it writes a Live
  layer's ring, in either product, and is 0 on a Sample layer, which has no writable buffer. So
  it is shown in the instrument too, where M1 hid it and mapped it to 0, and the instrument's
  Macro 7 acts on its Live layers. Since M2 sub-project 2 (§23 #68) it writes a Resample layer's
  ring too, so the instrument's Macro 7 and Feedback act on a Resample layer: the writable buffer
  the instrument lacked. With Overdub up, the frame written is
  `sanitize(in) + fb·limited[t − 256] + o·ring[R − P]`, in that order (Live ring buffer, below).
- **A Resample loop stays bounded:** the tap is bounded (`|out| ≤ 2 + |d|`), the ring clamps at
  ±2^20, Overdub ≤ 1, Feedback ≤ 0.95 and `|limited| < 1`. A Resample layer at Pitch 0, Position
  256 samples behind and Overdub 1 is a comb that can grow by the layer's Level per pass; the
  `[core][source][slow]` test renders that worst case for 60 s and requires it finite and
  bounded.

**Hard engineering rules**

- **64-bit positions everywhere.** Every position, scan pointer and grain phase is 64-bit:
  int64 + fraction (32.32 fixed point) or double, rebased each block. float32 is allowed only for
  offsets within a block. A float32 position breaks 1000× stretch after about half a second of
  buffer.
- **Deterministic CPU budget.** Each grain-sample costs a fixed weight per tier and channel count.
  When the budget runs out, the engine degrades in a fixed order:
  1. lower the interpolation tier for new grains;
  2. while the oldest sounding grain is under half its window, drop the new grain;
  3. past it, steal that grain with a pre-emptive fade.

  The order is by load, as Part I §8.4 reads, and D4 decides between dropping and stealing (the
  director, 2026-11-02; §23 #81).

  It **never** adapts to measured CPU load.

  **As built** (sub-project 3; the director's D1, D2 and D4, 2026-10-30; §23 #66):
  - **The weights** are integers in `kGrainWeights[tier][stereo]`, Normal stereo, X's case,
    being the unit of 4: Eco {2, 3} (mono, stereo), Normal {3, 4}, HQ {20, 27}. HQ's come from
    the measured HQ/Normal cost ratio, 6.79 (the median of three runs, rounded as `round(3r)` and
    `round(4r)`); the mono column is proportional and everything is provisional until M5's
    calibration. The weights never change at run time.
  - **Budget** (global, index `0x000050`, both products): 256 to 16,384 Normal stereo grains,
    stepped, default 4,096, saved with the session, automatable and a mod target. It is about
    40% of one M5 Max core at the default (X scaled) and 100% to 130% of a CI floor core. At
    each grid boundary where it, or the set of rendering layers, changes, the `Processor` gives
    each rendering layer an equal quota, `floor(4 × Budget / n)` units, computed serially before
    the layers' tasks run, so no pool or thread changes a bit. An engine with no quota has an
    unlimited one.
  - **The ledger** keeps the exact weighted load of the grains whose end is after the onset in
    the admission heap; admission counts only those, never the pool's size.
  - **The ladder,** for each new grain in this order: admit at the layer's tier if the load plus
    its weight fits the quota; else lower the tier (Normal before Eco); else, if no tier fits or
    the pool is full, **steal** the oldest sounding grain if it has sounded at least half its
    window (D4) and its weight makes room for some tier, and otherwise **drop** the new grain.
    So, by load: lower the tier; while the oldest sounding grain is under half its window, drop
    the new grain; past it, steal it with a fade (§8.4's order, read by D4; §23 #81).
    A steal moves the victim's end to the onset plus a 256-sample raised-cosine (sin²) fade (§23 #66, as
    amended; a grain with 256 samples or fewer left keeps its natural end and does not fade, ES-T6a)
    and is reported. A sample grain that would read past its sample's last frame ends there, faded
    over 256 samples (`kEdgeFade`), or over the span it has when that is shorter; at
    most one grain is stolen per admission. The room test of the steal excludes grains already
    fading (ES-T2a), so under sustained overload the stream is refreshed at its onset rate. It
    mostly steals and re-admits at the draw's tier, so lowering is rare once the oldest grains are
    half-way. It never clicks and never drops out; it never silences a layer.
  - **The bounded overshoot:** the stolen grain's weight stays in the ledger until its new end,
    so the real cost may exceed the budget by one grain's weight per steal in flight, at most
    `kStealFade` × the onset rate grains: at 16,384 onsets a second, about 87 grains, 2% of the
    default budget (8.5% of the exit case's 1,024-grain quota). The excess is bounded and
    deterministic.
  - **Every decision** is a function of the onset, the grains' latched ends and weights, the
    quota at the last grid boundary and the weight table: no clock, load, thread or call boundary
    enters, so the ladder is block-size invariant (the exit case: 16,384 one-second grains into
    one engine of capacity 16,384 at a quota of 1,024 grains, identical bytes and counts at five
    block sizes).
- **Mipmaps for upward transposition.** Built offline for files. Built incrementally with
  halfband decimators for live, oscillator and resample buffers.
  - **As built for files** (sub-project 3; D5, 2026-10-30): `PreparedSource` builds, off the
    audio thread at load, a pyramid with **one copy per level** (interleaved pairs for a stereo
    source, the planar channel for mono), each level the one above it passed through a
    linear-phase **55-tap half-band** (Kaiser β 8.0, 80.2 dB stopband, passband within 0.1 dB to
    0.4 of the new rate) and decimated by 2. A level is built while it would hold at least 64
    frames, to 5 levels (decimations 1 to 16), so a stereo file goes from 2× to 3× its audio in
    memory (§21); building costs about 1.75 s for a 10-minute stereo file. **Only HQ grains read
    above level 0:** a grain latches `level = clamp(floor(log2 rate), 0, 4)` at its onset and
    reads that level with its position and rate scaled by an exact power of two and the sinc
    stretched by the residual rate, so Eco and Normal grains, and every M1 golden, keep their
    bits. A source with fewer levels (a short file, a swap) is read at whichever level it offers.
    The anti-aliasing is weakest where the residual rate is just under 2 (about −37 dB),
    strongest at exactly an octave (§23 #66).
  - **As built for the live and resample rings** (M2 sub-project 2's Task 12, 2026-10-30): the
    ring's storage (`RingStorage`) holds **levels 1 to 3** beside level 0, each `ceil(C / 2ⁿ)`
    slots of interleaved pairs with the same 15 guard slots, made by the same 55-tap half-band
    in the same order of summation, so a ring's level holds the bits a file of the same frames
    gets. `LiveSource::write()` advances them as each frame arrives: level n's frame m, at ring
    frame m × 2ⁿ, is complete (2ⁿ − 1) × 27 ring frames behind the head, 27, 81 and 189 for
    levels 1 to 3. The ring builds the levels whose lag is within the 256-frame head margin M,
    so **three levels, not four**: level 4's lag would be 405 (the spec's §6.9 was written for
    the 31-tap design, whose four levels fit), and an HQ live grain at a rate of 16 or more reads
    level 3 stretched. A level needs no admission change: the clamp keeps every grain at least M
    behind the head, so every level frame a grain may read is complete. The live kernels keep
    the head, the window and the fade in ring frames and take only their taps from the level
    (`renderLiveLevelRef`, beside the level-0 path); a level read's fade covers its reach in ring
    frames, `(kBefore + 1) × 2ⁿ − 1` before and `kAfter × 2ⁿ` plus the level's lag after, so it
    reaches 0 before any of its taps leaves the window or is incomplete. **The head taper:** a
    level grain's fade toward the head therefore ends at the level's completeness edge, up to
    245 ring frames short of level 0's at level 3 (its reach, 253, less level 0's 8), so a
    grain running at the admission clamp's limit tapers over at most about 35 output samples
    of its approach to the head (it closes on the head at rate − 1 ≥ 7 frames a sample there),
    where its window is already closing:
    smooth, never a click, and level 0's fade is unchanged. Frozen, the levels freeze with
    level 0. **A resize copies the levels** as it copies the frames (`copyLevels`, by contiguous
    runs: the main thread's stable part, then the audio thread's at most 8,192 newest frames),
    which is bit for bit what computing them would give, and the copied level frames whose
    reach crosses the copy's oldest frame keep the old ring's values; computing the stable part
    instead (73 ms for 60 s, 368 ms for 300 s) outlasted the 170 ms in which the audio thread
    writes the 8,192 frames an adoption copies, so every offer of a long ring was declined (the
    review of Task 12; the plugin test that resizes 300 s to 200 s and back while the audio
    thread runs in real time pins it). **A restore computes them** (`fillLevels`, main thread)
    from the restored frames, ring time restarting at 0: they are a file's levels of those
    frames, so they equal the saved ring's bit for bit when the saved window began at frame 0,
    and otherwise hold the same audio away from the oldest ±lag frames, on the decimation grid
    of the restored frame 0. The SIMD and AVX2 live kernels match the scalar reference bit for
    bit at every level and tier. Writing the levels costs about 51 multiplies per frame (the
    half-band's 29 nonzero taps, the center's 0.5 included, both channels, at 7/8 of a level frame
    per ring frame); the levels add seven-eighths again to a ring's memory, which the cap counts.
    Only HQ grains read them, so the one golden that moves is `fx-hq-live` (until then
    `fx-hq-live-level0`), whose fold-back above Nyquist the level removes.
- **No clicks at extremes.** Per-grain parameters are latched at onset. Global parameters are
  smoothed. Window length has a minimum. Reset and freeze are declicked.

**Buffers and memory**

- **Loading** (M1 sub-project 3; §23 #48).
  - Sources live in RAM. Disk streaming doesn't suit random-access grain reads.
  - Cost is about 23 MB per minute for float32 stereo at 48 kHz, and twice that with the
    interleaved copy the engine reads (§23 #46).
  - **Formats:** WAV (RIFF, RF64 or W64; PCM 8–32-bit or float), AIFF/AIFC, FLAC and MP3, decoded
    with dr_libs. The format comes from the content, never the extension. A mono file stays mono;
    a wider one keeps its first two channels.
  - **M1's cap was 600 s** of the file's own audio, whatever the host's rate, checked against
    the header before anything is allocated, and against what a FLAC, MP3 or ADPCM decode
    yields, whatever its header says. **M2 sub-project 2 removed it** (`kMaxFileSeconds`; §23
    #68): a decode is bounded by the bytes the instance's memory cap leaves free (Memory
    management, below), checked the same two ways, and the cap's ceiling, 16 GiB
    (`kDefaultDecodeBound`), is the decoder's absolute bound. A refusal reads *needs N MB, the
    memory cap allows M MB*, or *decodes to more than the memory cap allows (M MB)* for a
    compressed file that overruns, after the file's name.
  - **Decoding memory follows what the file holds** (ruling R7, amended by §23 #54).
    Uncompressed audio is allocated once, at most at what the file's bytes can hold. A FLAC, MP3
    or ADPCM header is trusted up to 64 bytes of decoded audio per byte of the file: within
    that, the audio decodes straight into one buffer of the header's count and is held once.
    The cost: a header that lies within the ratio can cost up to 64× the file's size before the
    decode proves the file truncated. Beyond the ratio, or with no count, decoding grows as it
    goes, to about twice the audio that decoded. The memory cap is the real bound (since M2
    sub-project 2).
  - **A truncated file is refused** (ruling R8), with a tolerance for what an intact file can
    fall short by:
    - uncompressed WAV and AIFF: a declared audio size up to 1 KiB beyond the end of the file,
      1024 bytes included, as some writers leave it a little large (rulings R10, R12). The frames
      the file holds are decoded;
    - FLAC: none, except that a FLAC with no frame count decodes to its end;
    - MP3: two MP3 frames (2304), for encoder delay and padding;
    - ADPCM WAV: one block.
  - **Conversion to the host rate** on load, with r8brain-free-src (`CDSPResampler24`, linear
    phase, its latency compensated). The output has exactly
    `llround(frames × hostRate / fileRate)` frames, and the same input gives the same bits.
  - **A reactivation at a new rate converts synchronously,** in `activate()`, before it returns:
    the file is decoded again (the decoded original isn't kept) and converted on the main thread
    (ruling R18). There is no background reconversion, which would make output depend on the
    worker's speed. If the file has gone or changed, the old source keeps playing at the right
    pitch, since each grain's rate is scaled by source rate / output rate, and the status reads
    Missing or Changed.
  - **Interactive loads run in the background.** One worker thread per instance decodes, hashes,
    converts and builds the source for a load, a drop or a relink; a newer request supersedes
    one still waiting or under way. A session restore is synchronous instead (§18, D8).
  - **The source is immutable once built:** the planar buffer and the interleaved copy
    (`PreparedSource`), every page written off the audio thread. The main thread offers it
    through a lock-free mailbox; the audio thread adopts it at the start of a `process()` call
    by a pointer swap and hands back the one it retires, which the main thread frees.
  - **The swap doesn't click.** Grains that started on the old source finish on it; grains from
    the next onset read the new one.
  - **The exchange and the engine hold at most three sources at once:** the one draining, the
    one playing and one waiting for adoption. A further one waits until the oldest drains, at
    most Size later, and a newer offer replaces one still waiting. Between two `process()` calls
    a fourth can exist for up to one block: an offer made while one waits for adoption, until
    the next call takes it and hands the waiting one back (§23 #49). Besides those, the loader's
    worker may briefly hold the one it is building, and a superseded one until it notices; both
    are freed off the audio thread.
- **Memory management** (built in M2 sub-project 2, 2026-10-30; §23 #68). A process-wide sample
  pool removes duplicate copies of the same file, and each instance has a memory cap, shown in
  the UI since M2 sub-project 6's PR 2 (the sample strip's readout).
  - **The cap** is a per-instance setting, `memoryCapMiB`, saved in the state's `INST` chunk
    (§18), not a parameter (a host shouldn't automate memory): **default 1 GiB, floor 64 MiB,
    ceiling 16 GiB** (the director's D2, option 1). Lowering it evicts nothing; it refuses what
    comes next.
  - **What it counts** (`gg::media::MemoryBudget`, pure arithmetic on the main and loader
    threads): what the instance references, never what the OS has. Every layer's ring storage
    of record with its levels, every sample source a loader holds (planar, interleaved and its
    levels), a ring held for the next activation and a capture loaded into a layer. A source
    draining in the exchange (the one the engine plays out after a swap, or one retired and not
    yet collected) isn't counted, so a refusal never depends on when the main thread collects a
    retired source (§23 #68, ruling SP-T7b); the cost is that an instance can hold one sample's
    bytes past its cap for a grain's length. A load or a ring of a new length is checked with
    what it replaces still counted, since both live until the swap. A shared pool entry counts
    in full in every instance that holds it, so a refusal never depends on another instance.
    The fixed costs (the pools, the reverb, the event buffers and sub-project 4's Spectral and
    delay buffers, about 34 MB at 48 kHz; Layers and the chain, above) are documented, not
    counted.
  - **What it refuses, deterministically, with a status:** a load or a relink whose decoded
    size (from the header, with ruling R7's 64× trust for compressed files, then checked against
    what decodes), at the active rate with its interleaved copy and levels, would take the
    instance past the cap; a Ring Length whose storage would; a restore's ring or sample that
    would (the state loads, the offending ring or sample is left out). The check comes before
    anything is allocated; what is in memory is never evicted to make room. The status reads
    *over the memory cap: needs N MB, F MB free of C MB* (the need rounded up, the free space
    down).
  - **The sample pool** (`gg::media::SamplePool`, one per process, main and loader threads, one
    mutex): prepared sources by key (SHA-256, size, the rate's bits), held weakly, so it keeps
    nothing alive and the last instance to let go frees the entry. Ten instances of one project
    hold one copy of a shared sample's planar buffer, interleaved copy and levels. Find and
    insert are one step under the lock, so two loaders that build one key at once share the
    first to arrive; a source prepared for another rate is refused. Rings are per instance and
    never pooled.
  - **By example, at 48 kHz:** a 16 s stereo ring is 6 MB, 11.5 MB with its three levels; a 300 s
    one 115 MB, about 216 MB with its levels (Mipmaps, above: each level n adds ceil(C / 2ⁿ)
    slots, so the three add seven-eighths of level 0); four 300 s rings with levels are about
    860 MB, under the default with little room for a long sample, which is the point of the
    cap.
- **Live ring buffer.**
  - Each block writes before it reads.
  - An admission clamp keeps grains from reaching the write head.
  - The head-crossing policy is the admission clamp with the 256-frame head-proximity fade
    (Cloud core rules, below). Any other policy would be a later, explicit parameter.
  - Freeze and overdub use smoothed gains and crossfaded seams.
  - **As built in M2 sub-project 2** (2026-10-30; §23 #68), the ring is an instrument: its
    length a parameter, a window narrowed by a loop or a splice, overdub, splice marks and
    capture, each a function of the sample clock and the parameter history, applied on the
    control grid, and at its default the identity on M1's bits. The parameters are five per
    layer at `0x..40`–`0x..44` (§18).
  - **Ring Length** (1 s to `kRingMaxSeconds` = 300 s, the director's D3, option 1; default
    16 s) is neither automatable nor modulatable by its flags, nor a route's target (ruling
    SP-T12c, §23 #68), because its storage can't be allocated on the audio thread. The frames
    live in a `RingStorage` apart from the source.
    The plugin's main thread watches each layer's requested length
    (`Processor::ringSecondsRequested`, published at the
    grid boundary), allocates the new storage, the cap permitting, copies the **stable part** of
    the current window into it with the ring read below, and offers it with that head `R₀`
    through the layer's `SourceExchange`. The audio thread adopts it at a call's start: it copies
    the frames written since `R₀` itself, at most `kAdoptCopyFrames` = 8,192 (the newest 64 KB
    per channel), and swaps the pointer; the old storage goes back to be freed. An offer whose
    gap is past the bound is declined, and the main thread makes it again from the current head.
    Grains sounding read the same frames from the new storage, so a resize is inaudible; a grain
    reading older than a shorter new window could not be faded (its frames aren't in the new
    storage), so a shrink waits for it (ruling SP-T12ad, 2026-10-30): from the boundary that
    applies the shorter Ring Length, every grain admitted reads only the newest N′ frames (less
    64, the sinc's reach, until the reads narrow), the reads and overdub's period narrow to N′
    once no sounding grain can reach farther for the rest of its life (the period crossfaded
    over 10 ms), admission and Position never reaching past the reads (a longer Ring Length
    lifts them when the reads widen), and only then does the watcher offer the shorter storage,
    showing *resizing* meanwhile; whenever it lands it steps nothing and changes no output, and
    the shrunk ring admits as a fresh one of its length. After the swap the
    engine's Position and Spray reach the new ring (`Engine::ringResized`). A resize lands at a
    call's start, so it is part of a block-size-invariant render only when made before the first
    call, as a sample swap is; `gg-render` sizes every ring there. The status reads *resizing*
    meanwhile, or the cap's refusal. The exchange holds at most one storage in flight per layer;
    a restore replaces an unadopted resize.
  - **The window** is `[E − W, E − 1]`, the window length W generalizing N: with no splice
    selected E is the head R and W is N, or `min(L, N)` while **Loop** is on with **Loop Length**
    L (50 ms to 300 s, clamped to the ring), so a loop is the newest L frames. The admission
    clamp uses W wherever it used N, so every grain starts, and while the window holds stays, at
    least 256 frames inside it, and no grain reads across its ends. The head-proximity fade and
    the reads keep M1's bounds, the storage's `[R − N, R − 1]` (ruling SP-T12aa, 2026-10-30): a
    grain admitted under one window finishes in its old span when a loop or a splice change
    narrows, moves or widens the window, while the grains after it are admitted into the new
    one, so a window change never clicks. (A per-sample fade against the narrower window, as
    first built, stepped a sounding grain: 84 to 2,955 against the click limit of 15.) Loop on an
    unfrozen ring is a sound-on-sound looper (the director's D5,
    option 1): the input keeps entering, and Overdub recycles each frame leaving the window;
    Loop alone (Overdub 0) only narrows the scan window, and Freeze then holds the loop.
  - **Overdub** (0–100%, the gain o, ramped per sample over 10 ms like Feedback): each frame
    written is `sanitize(in[t]) + fb·limited[t − 256] + o(t)·ring[R − P]`, stored as any input is
    (non-finite as 0, clamped to ±2^20), with the period P = `min(L, N)` while Loop is on, else
    N: the frame leaving the window, still in storage (the slots hold N + B). So Overdub without
    Loop is a ring-length sound-on-sound. A piece whose Overdub ramp is 0 throughout writes by
    M1's path, so Overdub 0 is bit-identical to M1, the sign of zero included. Freeze wins, as
    over Feedback: frozen, nothing is written.
  - **Splices:** at most `kMaxSplices` = 32 marks per layer, ring-time positions kept ascending
    in a fixed array. A mark m leaves storage once `R − N > m`, decided per sample from R alone,
    never per block (ruling SP-T1a), and is dropped at the next write. **Splice** (stepped 0–32)
    selects one: splice k ≥ 1 is the span from the k-th mark still in storage, oldest first (the
    director's D6, option 1; ruling SP-T1b), to the next, or to the head for the last; the
    window is that span, Position and Spray reading seconds behind its end. A request past the
    count is the last splice, and the selection falls back to the whole window only when no mark
    is left; 0 selects the whole window (the loop's, with Loop on). On a live ring the numbering
    shifts as marks age out; on a held ring, the normal case, it is stable and automatable.
  - **Commands on the grid:** a `CommandEvent` (`AddSplice`, `ClearSplices`) joins its boundary's
    group after the group's parameters, in order. `AddSplice` marks `value` seconds behind the
    window's end (the head, or a selected splice's end), clamped into the window; a mark on an
    existing one, or a 33rd, is ignored. At most 64 commands apply per group
    (`kMaxGroupCommands`, twice the marks, so a full set and a clear always fit); later ones are
    ignored. A command on a layer that reads a sample is ignored. The plugin sends them through
    the editor's 256-entry command queue, applied from the next `process()` call's first
    boundary (a `params.flush` leaves them queued); `gg-render` takes them in `operations`.
  - **The ring parameters act whatever the layer's kind** (ruling SP-T5c): a Sample layer's ring
    keeps recording for a later switch (sub-project 1), so its Loop, Loop Length, Overdub and
    Splice shape the ring a switch to Live then hears; only the commands are gated on the kind.
  - **The ring read** (main thread, without stopping the audio thread), a seqlock's reader: the
    audio thread stores each layer's head with release order after every piece's write, and
    bumps a storage epoch, odd during an adoption or a reset, around a change. The reader takes W
    and the marks from the layer's published `RingMeta`, the epoch, the storage, then `R₀` from
    the head atomic with acquire order; copies frames `[max(0, R₀ − W), R₀)` from their slots;
    issues an acquire fence; then reads the head `R₁` and the epoch again. The copy is the window
    as of `R₀` when the epoch held and `R₁ − max(0, R₀ − W) ≤ N`; otherwise it copies again (ruling
    SP-T5a: up to one block is written past the published head, so the check is the window's
    length N, not the storage's capacity C). A frozen ring passes at once.
  - **Capture** reads a layer's window, or a splice of it, on the main thread with the ring read,
    into a stereo `SampleBuffer` at the host rate, which then goes wherever a loaded sample goes:
    a layer's sample through its loader (the pool and the cap apply), the session's `MEDI` as an
    embedded or filed capture (§18), or a float32 WAV the user names. The span is the **loop
    window**: the newest `min(L, N)` frames while Loop is on, else the newest `min(head, N)`, from
    the Loop and Loop Length base values; then the capture's splice argument, 0 for that whole
    window and k counting from the oldest mark inside it (ruling SP-T8a). The Splice parameter's
    selection doesn't narrow it. The plugin's API is `captureRing`, `loadCapture` and
    `saveCapture`, main thread only, for sub-project 6's gestures.

**Cloud core rules** (M1 sub-project 1, the FX slice; §23 #36)

- **The control grid** is 32 samples, aligned to absolute sample positions.
  - A parameter or modulation event takes effect at the first multiple of 32 at or after its
    absolute timestamp. The last event per parameter wins.
  - A boundary that falls exactly at a call's end applies in the next call (ruling M1-u).
  - Mix and Level ramp linearly to each new target over 10 ms, per sample, as a function of the
    absolute sample. A ramp restarts from its current value only when that parameter's target
    changes (ruling M1-s). Diffusion, Reverb Amount and Dry to Reverb ramp the same way (Space,
    below), and so does Feedback (Feedback, above).
- **The Cloud scheduler.**
  - Onsets: `t(k+1) = t(k) + Δ(k)`, with `Δ(k) = max(P × ((1 − d) + d × E(k)), 1/16 sample)`
    (ruling M1-g).
    - `P = sampleRate / Density`, and `d` is Distribution in [0, 1].
    - `E(k) = −logDet(1 − u(k))`, with `u(k)` keyed by (seed, layer, stream, `Field::Onset`, k).
    - The mean interval is `P`, up to the floor: periodic at `d` = 0, Poisson at `d` = 1.
    - The 1/16-sample floor bounds each call's loop, and it never applies at `d` = 0.
  - `logDet` is a deterministic natural log: argument reduction and a polynomial, with no libm
    apart from the exact `frexp`. It is within `1e-14 × max(1, |log x|)` of `log x` across (0, 1].
  - Density ≤ 0 means off. Positive values clamp to [0.01, sampleRate] Hz (ruling M1-a, as
    amended by §23 #45). The plugin's floor is 0.25 Hz, so it is never off, and its ceiling is
    400 Hz.
  - **A Density or Distribution change at grid sample g rescales the pending onset:**
    `t(k) = max(t(k−1) + Δ_new(k−1), g)`, from the same draw `E(k−1)`, with no new RNG draw
    (ruling M1-h).
    - Without it, a Density raise from 0.25 Hz would wait for an onset drawn at the old density.
    - Caveat: after off → on, the rescale waits until a grain is emitted. The plugin can't reach
      that case (Density ≥ 0.25 Hz).
  - Every grain field is drawn at spawn, from the parameters in effect at its onset's grid cell.
    Each draw has its own `rng::Field`, keyed with the real layer and stream IDs.
  - **In Live mode, Position and Spray are seconds** behind the write head: the start delay is
    `Position + Spray × u`, with u in [0, 1) (ruling M1-k). Sample mode keeps a normalized
    position with bipolar jitter. A layer's mode is its Source's built kind (The Source kind,
    above).
  - **Per-grain equal-power pan:** `pan = Stereo × bipolar(u)`, with channel gains `√(1 − pan)`
    and `√(1 + pan)`. IEEE `sqrt` is correctly rounded, so the gains are deterministic without
    a polynomial sin/cos.
- **Latched gain normalization.** Each grain's gain is `1 / √max(1, Density × Size)`, with
  Density in grains/s and Size in seconds.
  - That is the expected overlap; uncorrelated grains add in power.
  - It is latched at spawn, so a moving Density can't pump the level.
- **The wet chain:** per layer, Σ grains → the layer's diffuser → × its Level → soft
  limiter → × its morph weight; then the layers summed in order (Layers and the chain, above)
  → mix with dry, `dry × (1 − m) + wet × m`, plus the reverb's return while it is active
  (Space, below).
  - **Soft limiter:** the identity up to |x| = 0.5 (−6 dBFS). Above, `sign(x) × (1 − 0.25/|x|)`,
    capped below 1, with slope 1 at the knee and no libm; NaN gives 0 (ruling M1-t). It is a
    first, minimal form of the global limiter.
  - **Dry path:** non-finite samples count as 0, and finite samples aren't clamped, so at Mix 0
    a normal float passes bit for bit (ruling M1-q) while the reverb is idle.
  - One `ScopedDenormalsOff` covers the whole processor call, the dry/wet sum and Space
    included.
- **Space** (M1 sub-project 4; §23 #50): the layer's diffuser and the global reverb. Per sample,
  with `w` the layer's grain sum, `d` the FX's dry input (as above), `m` Mix, `g` Level's gain,
  `δ` Diffusion, `a` Reverb Amount and `s` Dry to Reverb, the last three ramped like Mix:

  ```
  w'  = δ > 0 ? w + δ·(diffuse(w) − w) : w        (the diffuser runs every sample)
  wet = softLimit(w' · g)
  FX:          rIn = m·wet + s·d    out = d·(1 − m) + wet·m   [+ softLimit(a·r)]
  instrument:  rIn = wet            out = wet                 [+ softLimit(a·r)]
  ```

  - **Per layer since M2 sub-project 1:** the first two lines are each layer's, with its own
    `w`, `δ` and `g`, and `wet` in the last two is the layers' weighted sum (Layers and the chain,
    above). With layer A alone on and the pad anywhere, that sum is layer A's `wet` bit for bit.
  - **The bracket is added only on samples of an active reverb chunk** (the idle rule, below);
    otherwise `out` is computed exactly as before Space. So with every Space parameter at its
    default the output is bit-identical to before, the sign of zero included, and every earlier
    golden's WAV is unchanged. Every sample is finite, and |out| ≤ 2 + |d|. With the global delay engaged (Delay Amount above 0; §14, Extremes and the FX chain), the bound is 3 + |d|, since the delay's return, capped near 1 by its loop's soft limiter, adds to the wet signal; the limiter's Soft and Peak modes keep the output under their ceiling (the director's decision D-bound, 2026-10-31, option 1; §23 #69).
  - **The reverb hears what the listener hears:** in the FX, the wet signal scaled by Mix plus
    the dry input scaled by the send, so Mix 0 with the send up puts only the input in the room.
    The instrument has no dry signal; its reverb hears the wet signal.
  - **One output channel:** each engine's mono render is diffused as both sides, the reverb hears
    that channel's `rIn` on both, and the channel gets the return's left.
  - **The diffuser:** four Schroeder allpasses in series per channel, `v[n] = x[n] + g·v[n−D]`,
    `y[n] = v[n−D] − g·v[n]`, with g = 0.625. Their lengths are 4.1, 5.9, 8.3 and 13.7 ms on the
    left and 4.7, 6.7, 7.9 and 12.3 ms on the right, each rounded to samples and raised to the
    next prime (again while it equals an earlier one), so the eight are distinct primes at any
    rate and the two sides differ, which also widens. Plain float arithmetic per sample, with no
    libm and no RNG. It runs whatever δ is, so raising δ from 0 resumes the diffusion of what was
    already heard.
  - **The reverb** is Surge's Reverb 2 from sst-effects, patched (§15), 100% wet and at full
    width. Fixed: `rev2_diffusion` 1.0, `rev2_buildup` 1.0 and `rev2_lf_damping` 0.2, kept at
    listening gate #1 (§23 #60). Mapped: `rev2_room_size` = 2 × Size − 1, `rev2_hf_damping` =
    Damping, `rev2_modulation` = Modulation and `rev2_decay_time` = Decay in log2 seconds.
    Reverb 2's own decay formula is kept, so the measured T60 at Damping 0 runs longer than
    Decay's label: 1.72, 2.89 and 5.09 s at 1, 2 and 4 s (ruling R-T5c).
  - **On the grid, 32 samples late.** Reverb 2 processes whole 32-sample chunks, and they are
    the control grid's cells: chunk k holds absolute samples [32k, 32k + 32) of the engine's
    clock, which a reset restarts together with the reverb's. A chunk runs when its last sample
    arrives, and its output is returned during the next chunk, so the return is a constant 32
    samples late at every block size. The parameters are latched at each chunk's first sample, a
    grid boundary. The 32 samples count toward Pre-delay: the reverb's own pre-delay is
    `max(0, Pre-delay × sr − 32)` samples, so Pre-delay's real minimum is 32 samples. No latency
    is reported: only the return is late. Reverb 2's own taps put its first audible return about
    60 ms after the input, at Pre-delay 0 and Size 50%.
  - **The reverb's onset** (row 302; `core/src/Processor.cpp`, `kReverbFade`). At an idle-to-active
    transition of the reverb after t = 0 (Reverb Amount rising from exactly 0), the reverb's
    input is faded in with a raised cosine over 10 ms, so a room starting empty never hears a
    step; at t = 0 nothing fades. An Amount event at frame 0 counts as an onset from chunk 1, as
    the Amount ramp is still 0 at chunk 0.
  - **The idle rule.** A chunk is active when the Amount ramp isn't exactly 0 at its first
    sample. An idle chunk doesn't run the reverb and returns +0, and the bracket isn't added.
    The first idle chunk after an active one starts clearing the reverb, and each idle chunk
    zeroes at most 16,384 floats of it, 64 KiB (ruling R-T5a); the first active chunk finishes
    whatever is left, then runs. So raising Amount starts from an empty room, and from there
    renders exactly what a fresh instance would. The rule depends on the chunk sequence alone,
    never on time, CPU or the signal. Dry to Reverb alone doesn't wake the reverb.
  - **The clear zeroes only what a later read can reach** (ruling R-T2b): each allpass up to its
    longest length since the last clear, and of each delay line and the pre-delay only the
    oldest positions that a fresh run reads before writing them, within the reach of the largest
    Size, Modulation and Pre-delay at the current rate. A whole clear at 48 kHz is about 0.5 MB,
    not the 14.5 MB the reverb holds. `reset()` clears it at once, a cost hosts expect of a reset
    (ruling R-T5b); construction is already clean.
  - **Memory:** 14,533,632 bytes of fixed arrays per instance (about 14.5 MB), allocated with the
    `Processor`, off the audio thread. A new sample rate builds a new `Processor`, and with it a
    new reverb.
  - **Pre-delay glides** (S8): its read moves toward each new value at no more than 0.5 samples
    per sample, interpolated linearly, so a moving Pre-delay bends the pitch at most an octave
    down or a fifth up, like tape, and never clicks (ruling P1). While nothing has run since
    construction or a clear, a new value applies at once (ruling R-T2a). A glide leaves a
    broadband haze near −90 dBFS.
  - **Size isn't smoothed** (S8): a change takes effect at the next chunk, as in Surge, and
    mid-sound it clicks or glitches the pitch. Accepted: the FX view's knob sets on release
    (D10, §23 #71; row 217 closed). Automation still clicks.
  - **Infinite Decay** (S5, S7): Decay's top 2% of travel, from log2 60 s (`kDecayInfinite`), is
    ∞. There the loop's decay multiplier is exactly 1; the LF dampers become a 0.5 Hz DC
    blocker, the same one-pole high-passes, with their own state, at `kInfiniteDcHz` (ruling
    R-T8a), since a lossless loop integrates the grains' DC; the HF damping follows Damping with
    its lower clamp at 0, so at Damping 0 the loop is lossless apart from DC; and `loopClip`
    bounds each block's signal after the dampers: `x` for |x| ≤ 1, else `sign(x) × (2 − 1/|x|)`,
    below 2 in magnitude, with slope 1 at the knee and no libm. The input is still added.
    **Entering and leaving ∞ glide** over 30 ms (ruling R-T8b): block by block, the decay
    multiplier, the LF coefficient (geometrically) and the HF clamp move between their finite
    and infinite values, so neither switch steps the loop; a reverb that starts at ∞ starts
    there. An hour of a full-scale 110 Hz sine, and of full-scale noise, at ∞ with Damping 0 and
    Size and Modulation at 100% stays finite and bounded (the `[slow]` tests; peaks 1.685 and
    1.718). Every finite Decay keeps Surge's loop unchanged.
  - **Damping and the rate:** Reverb 2's damping coefficients don't scale with the sample rate,
    so how fast the highs fade at a given Damping depends on the host's rate. That is Surge's
    behavior, and it is kept.
- **The live ring,** one per layer, in both products since M2 sub-project 1 (the instrument's
  fed from its sidechain; Layers and the chain, above).
  - Its window is N = 16 s of stereo frames, `[R − N, R − 1]` for write head R. It is stored in
    N + maxBlockSize frames, so a block written whole before it renders never overwrites a frame
    still in a sample's window. Output is independent of the host block size and of
    maxBlockSize (ruling M1-o). Since M2 sub-project 2, N is the layer's Ring Length (16 s by
    default) and the readable window is `[E − W, E − 1]`, narrowed by Loop or a selected splice
    (Live ring buffer, above); at the defaults it is this one, bit for bit.
  - Non-finite input is stored as 0, and finite input is clamped to ±2^20; in-range audio is
    unchanged (ruling M1-p).
  - **The admission clamp** sets the grain's start delay so that every Hermite tap
    (p − 1 … p + 2), over the grain's whole life, stays at least M = 256 frames inside the
    window: behind the head even at rate > 1, and ahead of the oldest frame.
    - It is exact through the 20 ms freeze ramp, with the head measured at the sub-sample onset
      (ruling M1-p).
    - When both limits can't hold, the grain is dropped and counted.
  - **The head-proximity fade:**
    `g = clamp((R − 1 − (p + 2)) / M, 0, 1) × clamp(((p − 1) − (R − N)) / M, 0, 1)`.
    Frozen, the window's two ends are the seam where the newest and oldest frames meet, so this is
    the head-crossing crossfade.
  - **Freeze** ramps the write gain to 0 over 20 ms, then stops the head. Unfreeze resumes the
    head and ramps the gain back over 20 ms. The ramp is an exact integer count, so it doesn't
    depend on where blocks fall.
  - **Freeze is live until toggled** (§23 #40; ruling M1-ac), each layer's Freeze arming on its
    own since M2 sub-project 1. A ring that wasn't saved starts empty, so the parameter engages
    the engine's freeze only once armed, and it arms by being seen off after the activation or
    the last reset. **A restored ring is the exception** (M2 sub-project 2; §23 #40 amended by
    §23 #68): a session saves a layer's ring when it is held, its Freeze or Loop base value on,
    on a layer whose base Source builds to Live (Live or Resample; the director's D1, option 1,
    with the final review's amendment; §18), and a layer whose ring is restored arms at the load, so
    a Freeze that is on holds the restored ring from the first sample (ruling SP-T1c: the ring
    restores with the head stopped and the write gain 0, no ramp). A layer whose ring wasn't
    saved (an M1 state, a plain live ring) keeps the rule below, ruling FR-8 unchanged:
    - Freeze is seen where it reaches the engine: each call's first sample, each applied grid
      boundary, and a `setBase` of Freeze itself (params.flush).
    - A state load arms from its loaded values alone, every layer alike (`Processor::setBases`,
      since M2 sub-project 1's fix wave; ruling FR-8, §23 #65): a layer whose loaded Freeze is
      off arms, and one whose Freeze is on stays live until toggled if not yet armed (a fresh
      ring, after an activation or the last reset), while one already armed, as a running
      instance's usually is, freezes at once.
    - An activation and a reset disarm it, and the reset itself doesn't arm. So a Freeze that is
      on when the ring starts, restored with a project, kept through a reactivation, or turned on
      after a reset (a VST3 suspend, say) before audio flows, leaves the effect playing live
      grains until it is toggled. One that is off at the next call's first sample arms.
    - Modulation that takes the effective Freeze off and back on counts as a toggle.
    - The parameter and its saved state still read On while it isn't holding.
    - A boundary's events apply as one group, so off and on again at one boundary is no
      transition, and the result doesn't depend on block size.
    - A note-on needs no arming (Notes, below).
- **Notes** (M1 sub-project 3; §23 #48). Both products take notes, and every layer hears them
  (Layers and the chain, above). Velocity does nothing to the grains (D6); since M2 sub-project 1
  it reaches the mod matrix's Velocity source, and nothing else.
  - **Input:** CLAP note-on, note-off and note choke (a note-off), and MIDI note-on, note-off
    (and note-on at velocity 0) and CC 123. MPE's per-note channels are just channels.
    - A CLAP note-off with wildcard (−1) fields releases every held note it matches.
    - **One addressed by `note_id` alone** (channel and key −1) releases that ID's note: the ID of
      each CLAP note-on that has one is remembered in a fixed 256-entry table on the audio
      thread, the oldest replaced when it is full. An unknown ID releases every note (ruling R15).
    - **CC 123 releases only its own channel,** as MIDI 1.0 says; an all-off on channel −1
      releases everything (ruling R14).
  - **On the control grid:** a note event at absolute sample a takes effect at the boundary
    ceil(a / 32) × 32 and joins that boundary's group, with its parameter events. A group's
    notes apply in order, and only the held set after the group counts, so a note that starts
    and ends within one group does nothing.
  - **The held set** is the held (channel, key) pairs. A key held on two channels counts once,
    and stays held until both are off. Two notes on one (channel, key) share it, so the first
    note-off releases both; M3's Voice scheduler revisits this. An activation or reset clears
    it.
  - **Each grain's note:** while n > 0 keys are held, grain k plays the key at index
    `min(floor(u × n), n − 1)` of the held keys, distinct and ascending, with `u` keyed by (seed,
    layer, stream, `Field::Note`, k). Every grain draws, however many keys are held; with none
    held, nothing is drawn, so no existing draw changes.
  - **Pitch:** the Pitch parameter plus (key − 60), clamped to [−48, 48] semitones. Note 60
    plays at the Pitch parameter alone, as with no note held; at Pitch 0 that is the source's
    own pitch.
  - **A note-on starts a grain at once:** when the held set goes from empty to non-empty at
    boundary g, the pending onset becomes g. A note added to a held chord doesn't move it. The
    grain index k keeps counting across notes.
  - **The instrument is silent until a note** (the note gate, on every layer): no grain spawns
    while nothing is held. From the boundary where the last note is released no grain spawns,
    and the sounding ones play out their window, a release tail as long as Size.
  - **Notes freeze the FX** (D4): each layer's engine's freeze is on while its Freeze is engaged
    (armed, as above) or any key is held, so a held key freezes every layer's ring. A note-on
    engages even right after a reset or a reopened
    project. Releasing every note unfreezes the ring unless Freeze is engaged. Notes never
    change Freeze's value, automation or saved state. Position and Spray keep reading seconds
    behind the stopped head.
- **A Sample layer's Position and Spray** (the instrument's in M1; by the layer's kind, not the
  product, since M2 sub-project 1). Position is the fraction of the loaded file, and Spray is
  seconds, bipolar around it, so its spread doesn't change with the file's length: a grain
  reads from
  `clamp(position × (frames − 1) + spray × bipolar(u) × sourceRate, 0, frames − 1)`. A Live
  layer keeps seconds behind the head for both (ruling M1-k), in either product.

**Extreme stretch.** Grains alone start to comb and sound static at 100× and beyond. Two tools
address this:
- a **Smear** control that couples position jitter, overlap (8 to 32), micro-detune and
  power-based normalization;
- a paulstretch-style **Spectral Smear** source (random-phase STFT on pffft) that grains can
  themselves read from.

**Extremes and the FX chain** (M2 sub-project 4, built 2026-10-30; §23 #69; spec
`docs/superpowers/specs/2026-10-30-m2-extremes-design.md`). Forty parameters in the reserved
`0x60`–`0x7F` blocks (§18), headless when built, like wave 1's: the host's generic editor,
automation, the mod matrix, the "FX" and "Filter A" to "Filter D" remote pages and `gg-render`
reach them, and since M2 sub-project 6's PR 2 the Edit view's Filter panel and the FX view (§17).
With every one at rest both products render M1's bytes: every
earlier golden keeps its WAV SHA-256.
- **Smear** (`0x..60`, per layer, 0–100%, default 0). With s its value, D the layer's Density, S
  its Size and every draw counter-keyed (`Field::SmearJitter`, `SmearDetune`; their numbers
  assigned at the rebase):
  - **Overlap.** The target is `T(s) = 8 + 24s`, and the scheduler runs
    `D_eff = D + s × (min(400, max(D, T(s) / S)) − D)`: continuous at 0, never below D and never
    above the plugins' 400 Hz ceiling.
  - **Position jitter** `s × 0.5 × S × bipolar(u)` seconds, added to the seconds behind the head
    on Live and Spectral layers and, on Sample layers, in seconds at the source's rate as Spray's
    are. It scales with Size, not Spray: the comb comes from overlapping grains re-reading the
    same material at the same offsets, and Spray is usually 0 at extreme stretch.
  - **Micro-detune** `s × 0.5 × bipolar(u)` semitones, after Pitch Spread's term, inside the ±48
    clamp.
  - **Power normalization** stays `1 / √max(1, D_eff × S)`, so the raised overlap doesn't pump
    the level.
  - At 0 nothing is drawn and no field changes. At any s > 0 the event line's `pos`, `rate` and
    `gain` carry Smear, so the cross-platform gate sees it. The director's D8, option 1: full
    strength at 100% (declined: a mild 16 / ±a quarter / ±20 cents).
  - **The exit criterion** is a pinned test, `tests/metrics/comb_tests.cpp`: two in-process
    instrument renders over noise at a static Position and at 1000×, Smear 0 and 1, scored by
    the cepstral peak at quefrency 1/D over the median cepstrum in 10–500 ms; Smear 1 scores well
    below Smear 0 (§19).
- **The Spectral Smear source** (the Source kind's Spectral; `engine/include/gg/spectral/`).
  At every hop it takes one window of the layer's material around Position, gives every bin a
  random phase from the counter RNG (`Field::SpectralPhase`, keyed by layer, the channel as the
  stream, frame and bin), resynthesizes it and overlap-adds it into a stream of its own, which
  grains read as they read a live ring.
  - **The STFT** has N the power of two nearest 85 ms at the running rate (4096 at 44.1 and
    48 kHz), hop N/4 (a multiple of 32, so a hop boundary is a grid boundary), Hann analysis and
    synthesis, on pffft (now also in the engine, §15). Frame k fires when the output clock
    reaches kH, from the material at the Position in force then; the stream is complete one
    window late, a constant. The synthesis divides by 0.75 (`kOverlapGain`): frames with redrawn
    phases add in power, so a steady material keeps its RMS within 1 dB (measured −0.14 dB). The
    smear is a function of the sample clock alone, at any block size. The channels' phases are
    keyed apart, so they decorrelate completely: center content comes out wide.
  - **The output ring** is a `LiveSource` of 5 s (`SpectralSource::kRingSeconds`), written one
    sample per sample on every piece, silence while the kind isn't Spectral, so a kind switch
    allocates nothing. A Spectral grain is a ring grain on its own slot (`kSpectralSlot` = 3), so
    the live kernels read it at every tier. Switching to Spectral fades the stream in over one window;
    switching away drains the pending overlap-add, then writes silence (a click otherwise).
  - **The material** (the director's D1, option 1): the layer's sample when it holds one, else its
    live ring (the FX's input; the instrument's sidechain). It is decided where the engine adopts
    a sample, not where one is offered: the `Processor` re-sends the layer's Cloud parameters,
    with the material's units, when a sample is adopted or removed, as on a kind change.
    Position follows the material: a fraction of the sample, or seconds behind the ring's head
    bounded as on a Live layer (with a floor of 1), and its text follows through
    `CLAP_PARAM_RESCAN_TEXT`. Spray spreads grains one-sided over the smear.
  - **Freeze** (D2, option 1) holds the material, not the smear: the layer's ring freezes as on a
    Live layer, Position scans it, and the smear keeps evolving with new phases. The spectral
    ring is never frozen. **Feedback is 0** on a Spectral layer, as on a Sample layer.
  - **Amended (D-specwindow, the director, 2026-11-01, option 1; §23 #69), built by the M2 close
    (row 381):** a Spectral layer over its ring follows the ring's loop window and splice
    selection, every analysis frame starting inside the window, and takes the ring's splice
    commands (`SpectralSource::computeFrame`). A window shorter than a frame plus the 256-frame
    margin leaves the frame at the newest start, as a Live grain admitted there is.
  - **DC and Nyquist reconstruct at unity** like every other bin (row 365): they keep their
    phase and add in amplitude across overlapping frames, so they are scaled by `kOverlapGain`
    before the synthesis divides it out. Before the close they came out about 2.5 dB high.
  - **Cost:** about 50 Mflop/s of FFT per Spectral layer at 48 kHz (§16 has the measured figure).
- **Buses and filters** (D3, D4). Each grain carries a bus bit, latched at spawn: it takes bus 2
  when its `Field::Bus` draw is under the layer's `Bus Split` (`0x..61`, 0–100%; the director's
  D3, option 1, chance per grain; declined: alternating, and by pan side), and at 0 nothing is
  drawn. The engine renders each bus in spawn order (a second output pair on
  `Engine::process`). Each bus has a filter (`Bus 1 Filter`, `Cutoff`, `Resonance` at
  `0x..62`–`0x..64`, bus 2's at `0x..65`–`0x..67`): Off, Low-pass, High-pass, Band-pass or Notch,
  a 12 dB per octave state-variable filter (D4, option 1: sst-filters' `CytomicSVF`; declined:
  a 24 dB cascade and a ladder), Cutoff 20 Hz–20 kHz log, Resonance mapped so every setting is
  stable.
  - **On the grid, smoothed:** each filter takes its effective cutoff and resonance at every
    32-sample boundary on the absolute clock and interpolates per sample, so automation and the
    matrix never zipper. The filtered audio is plain arithmetic, expected bit-exact across
    platforms.
  - **Exact bypass, by a sticky flag.** A bus whose type is Off passes bit for bit. Bus 2 is
    provably empty until a grain has been admitted to it: the engine raises `bus2Used()` at the
    first bus-2 admission and lowers it only in `reset()` and when the enable gate turns off
    (where the layer also clears its diffuser and Feedback history). While it is down the layer's
    sum is bus 1 alone, with no `+ 0`, so Bus Split 0 with both filters Off is M1's grain sum bit
    for bit, the sign of zero included; bus 2's filter hears silence meanwhile, which leaves it
    silent. Once up, the sum is `bus1 + bus2`, each after its filter, for the layer's run: a
    live count would cut bus 2's filter tail the moment its last grain ended.
  - **Declicked switches.** A type change starts the incoming type on a spare filter instance
    from rest and crossfades the bus over the 10 ms ramp, so no switch clicks. After a clear (a
    new instance, a reset, the layer switched back on) the first cell takes its type at once, as
    the ramps jump there.
  - They sit in the layer's task before the diffuser, so the diffuser, Level, the soft limiter,
    Feedback's history and the morph weight see the filtered sum where they saw the plain one.
    The new layer parameters are mod targets like any layer parameter: since sub-project 3, the
    matrix refuses a layer parameter only if it is a modulator's control or Ring Length
    (`isValidTarget`; ruling SP-T12c, §23 #68), no longer anything above `LFO 1 Rate`.
- **The global delay** (`0x000060`–`0x000064`: Amount, Time 1 ms–2 s log with default 375 ms,
  Feedback, Damping, Ping-pong), in-house and libm-free, after the layers' sum and before the
  reverb and the mix: `wet' = wet + Amount × y`, the line written every sample and the return
  added only where Amount's ramp isn't 0, so `wet'` is `wet` bit for bit at rest. The loop runs
  through M1's `softLimit` and a one-pole damper (20 kHz at Damping 0 to 500 Hz at 100%, log), so
  it is bounded below 1. Amount, Feedback and Damping ramp over 10 ms, and so does Ping-pong's
  switch, a weight that crosses the damper's input from the channel's own read to the other's,
  so a toggle never clicks. **Time glides** (D6,
  option 1; declined: a crossfade, a jump): the read position moves toward its target at no more
  than 0.5 samples per sample (Space's Pre-delay rule), so a move bends pitch like tape and
  never clicks, except inside a Feedback loop at 95% or more over a near-silent tail, where the
  re-read passes crackle: an accepted law (D-glide, the director, 2026-10-31; row 379).
  **Feedback reaches 100%** (D7, option 1; declined: the instrument's tail made
  infinite, a 95% cap), so the delay can hold a repeat as Decay ∞ does, and the instrument's
  reported tail stays its constant 122.5 s: a host that suspends an instrument after its tail
  cuts a held delay 122.5 s after the last note. The FX's tail is infinite already.
  **The hold darkens** (the director's decision D7a, 2026-10-30, option 2; declined: Damping 0
  bypassing the damper for an exact hold): at Feedback 100% and Damping 0 the damper is still a
  20 kHz one-pole in the loop, so bright material slowly darkens. A noise burst loses about 2 dB
  in the 2–4 kHz band over 20 s, and a sine burst holds within 0.1 dB.
- **The global limiter** (`0x000065`–`0x000067`: Mode, Ceiling −24…0 dB default −1 dB, Release
  10 ms–1 s log default 100 ms), after the reverb's return and before the mix, on the wet side
  alone: the FX's dry path stays unclamped (ruling M1-q). Off is the identity, by default, and
  M1's per-layer soft limiters stay. **Soft** is M1's `softLimit` scaled to the Ceiling. **Peak**
  (D5, option 1; declined: a hard clipper, a look-ahead limiter) is a zero-lookahead peak
  limiter: its gain, computed in double, drops instantly to hold the output at the Ceiling and
  releases with its time constant, with no latency; a single-sample peak is clamped rather than
  anticipated. A mode change crossfades over 10 ms, and snaps where the ramps jump (a loaded
  state, a macro or route, a reset). **Peak's instant gain steps score as clicks on hot
  material** at the bottom ceilings, an accepted law (D5b, the director, 2026-10-31; §23 #69):
  over the sweep's extremes corners (carry-over row 378) Peak at −24 dB scored 16.24 to 42.05,
  entering Peak while the Ceiling ramped to −24 dB 21.05, and Soft at −24 dB up to 19.77, against
  the limit of 15, and the sweep looks away there through `kLimiterDrive`; `fx-limiter` itself
  scores 4.31. **Entering Peak mid-sound** over hot material can click inside the mode fade,
  where Peak's gain leaves 1 with no attack (24.4 at 461 samples into the fade, at −18 dB): part
  of the same zero-attack law (the director, 2026-11-02, K-2; §23 #79), and the sweep exempts
  the mode fade by samples, not by where its calls fall. Off keeps M1's
  association, `out = (d·(1 − m) + wet'·m) + softLimit(a·r)`; only Soft and Peak form the wet side
  first and add it to the dry.
- **The chain,** per piece, extending Layers and the chain (above); new stages marked:

  ```
  per layer L (A..D), one task:
     ring_L[t]  = sanitize(in[t]) + fb_L(t) · limited_L[t − 256]
     spectral_L = Spectral kind ? smear(material_L, Position_L) : 0     (one sample per sample)  new
     b1_L, b2_L = Σ grains_L by bus, each in spawn order                                         new
     w_L        = F1_L(b1_L) ⊕ F2_L(b2_L)    (⊕: bus 2 added only once a grain has been on it;   new
                                              F: the bus's filter, or the identity when Off)
     w'_L       = δ_L > 0 ? w_L + δ_L·(diffuse_L(w_L) − w_L) : w_L
     limited_L  = softLimit(w'_L · g_L)
     wet_L      = limited_L · weight_L(t)
  instance, after the tasks join:
     wet   = wet_A ⊕ wet_B ⊕ wet_C ⊕ wet_D
     wet'  = wet [+ Amount · delayRead]       (added only where Amount's ramp ≠ 0)               new
     FX: rIn = m·wet' + s·d        instrument: rIn = wet'
     limiter Off (default): FX out = (d·(1 − m) + wet'·m) [+ softLimit(a·r)]
                            instrument out = wet' [+ softLimit(a·r)]
     limiter Soft or Peak:  FX out = d·(1 − m) + limit(wet'·m [+ softLimit(a·r)])               new
                            instrument out = limit(wet' [+ softLimit(a·r)])
                                             └─► the resample tap (2) reads out, as above
  ```

  The buses, filters and STFT run inside the layer's task on its own state, the delay and
  limiter in the serial tail.
- **Determinism and real time.** Smear's fields are latched per grain; the hops are absolute grid
  boundaries; the filters' coefficients change at grid boundaries; the delay's ramps and glide and
  the limiter's gain are functions of the absolute sample; output is bit-identical at every block
  size and with any thread pool. Every buffer and pffft's setup are allocated at construction. The
  Spectral audio differs in its last bits across SIMD flavors (pffft's NEON and SSE), covered by
  the goldens' metric tolerances as Reverb 2's `powf` is; the grain-event log, which sees no
  audio, stays exact.

**Oscillator sources.** Two kinds:
1. *Stream oscillators* rendered into ring buffers. These include band-limited basic waveforms
   and the MIT-licensed Plaits engines, excluding the speech engine's TI-ROM-derived data.
2. *Synthetic grains*: sine, wavetable, pulsar, trainlet, FOF-style and glisson grains. Each uses
   a mipmap level chosen from its effective frequency and resets phase per grain.

**Determinism**

- The seed lives in the plugin state. A new instance draws its own from the OS's entropy source,
  on the main thread (§18, §23 #59); the audio thread's randomness never touches the OS.
- RNG is counter-based: a stateless SplitMix64 mixer over a key of (seed, layer, stream, field,
  grain index) (`gg::rng`, `engine/include/gg/core/Rng.h`).
- Random distributions are written in-house. Never use `std::` distributions, because they differ
  between standard libraries.
- The control grid is 32 samples, aligned to absolute sample positions (Cloud core rules).
- No fast-math reductions in the audio path.
- No floating-point contraction: every first-party target compiles with `-ffp-contract=off`
  (gcc and clang) or `/clang:-ffp-contract=off` (ClangCL), so `a * b + c` is never fused into
  an FMA on one platform and rounded twice on another. Never enable fast-math. The flags are in
  `cmake/compiler-options.cmake`.
- Layers are summed in a fixed order, A to D, after their render tasks join, whatever thread
  rendered each (§15).
- Every modulation source is a function of the sample clock, the parameter history and the
  host's events; the random sources draw from the counter RNG (The mod matrix, above).
- The ring's operations (M2 sub-project 2; §23 #68) draw nothing: Loop, Loop Length, Overdub,
  Splice and the commands apply on the grid, a mark's aging is decided per sample, and the tap is
  kept by absolute time. A ring resize or restore lands at a call's start, as a sample swap does.
  The SIMD live kernel reads the window's end and oldest frame from `LiveSource`, so it stays bit
  for bit the scalar reference's under a loop or a splice (ruling SP-T1d).
- Any reused sst modulator or effect that seeds its RNG from the clock is patched to use the
  engine RNG. Reverb 2, the first reused effect (§23 #50), has no randomness: its modulation is a
  deterministic quadrature oscillator. sst-effects and sst-filters are header-only and compile in
  gg-core's own sources, under the flags above. Reverb 2's coefficients use `powf`, deterministic
  on one platform; across platforms the goldens' metric tolerances cover the last bits, and the
  grain-event logs, which the reverb never reaches, stay exact.
- Render quality changes only through an explicit user parameter, never automatically for offline
  rendering.
- The engine runs at the host sample rate.

### 15. Technology stack

**Language and build**

- **C++23** (since M2 sub-project 1, 2026-10-30; C++20 before; §23 #65) with CMake 3.22 or newer
  (JUCE 9 requires 3.22): `CMAKE_CXX_STANDARD 23`, required, for every first-party target
  (`cmake/compiler-options.cmake`); third-party targets that set their own standard keep it.
  **GCC 13 or newer** on Linux (the configure fails below it, naming the floor), Apple clang and
  ClangCL as before,
  and upstream LLVM 20 or newer for RTSan. No first-party code uses a C++23 library feature yet,
  and none that libc++ marks unavailable before macOS 11 may be used.
- Ninja everywhere: on Windows, Ninja with Visual Studio 2022's own clang-cl and MSVC's linker,
  from an x64 developer shell (until 2026-11-02, the Visual Studio generator with the ClangCL
  toolset; §23 #76).
- Universal macOS builds via `CMAKE_OSX_ARCHITECTURES=arm64;x86_64`, minimum macOS 11.0.
- Linux builds against an **Ubuntu 24.04 baseline** (§23 #91; Ubuntu 22.04 until 2026-11-03):
  the products need its glibc, 2.39, and don't run on 22.04 or another system with an older
  glibc. CI's leg, the `linux-gcc13` preset (`linux-gcc12` until M2 sub-project 1), runs on
  `ubuntu-24.04` with gcc-13 and g++-13 from that release's own archive (22.04 took them from the
  `ubuntu-toolchain-r/test` PPA). The Linux products link libstdc++ and libgcc statically, their
  symbols kept local (begun for 22.04, whose libstdc++6 lacked the GCC 13 floor's
  `GLIBCXX_3.4.32`; §23 #65), so the system's libstdc++6 and a host's don't matter and glibc is
  the floor. CI's leg checks the ceilings, read from the runner's own `libstdc++.so.6` and
  `libc.so.6` once both packages are confirmed as Ubuntu 24.04's, and requires the runner's
  glibc to be exactly 2.39: no product may need a `GLIBCXX`, `CXXABI` or `GLIBC` version above
  them, or export a `std::` symbol.

**Plugin architecture: CLAP-first.** This follows the Shortcircuit XT pattern. The working
templates are in its repository, https://github.com/surge-synthesizer/shortcircuit-xt, at
`src/clients/clap-first/CMakeLists.txt`, `libs/CMakeLists.txt` and
`src/scxt-plugin/CMakeLists.txt`.

```
engine/   framework-free, real-time-safe, headless C++ — also powers gg-render
core/     JUCE-free gg-core: patch, parameter mapping, Processor — shared with gg-render
media/    JUCE-free gg-media: decoding, rate conversion, SHA-256, sample search, settings file
plugin/   CLAP implementation: params, notes (MPE in M3), state, sample loader, UI↔audio queues
ui/       JUCE editor (GrainCanvas behind a renderer interface)
clients/  CLAP entry points + clap-wrapper targets
```

- **The `core/` layer (`gg-core`, since M1; §23 #38)** sits between the engine and the plugin. It
  holds the patch and its versioned state XML (public API `gg/core/PatchState.h`), the only
  parameter → engine mapping (`ParamMap`), and the `Processor`: the control grid, the ramps, the
  FX's live input, the notes, the source hand-off, the wet chain, Space (the diffuser,
  `gg/core/Diffuser.h`, and the reverb, `gg/core/SpaceReverb.h`, since sub-project 4; §23 #50)
  and the dry/wet mix; since M2 sub-project 1 the four layers, the morph, the render tasks and
  the mod matrix with its sources (`gg/core/ModMatrix.h`, `gg/core/ModSources.h`; §23 #65), with
  the input feeding every layer's ring in both products. The plugin and `gg-render` drive the
  same `Processor`, so the goldens test what a DAW plays. Since sub-project 3 it also holds the
  session state container
  (`gg/core/SessionState.h`, §18) and the per-machine settings' XML
  (`gg/core/MachineSettings.h`). It has no JUCE.
- **The `media/` layer (`gg-media`, since M1 sub-project 3; §23 #48)** is sample-file I/O, never
  on the audio thread: decoding (`gg/media/Decode.h`), rate conversion (`Convert.h`), the
  SHA-256 fingerprint (`Sha256.h`), loading a sample with its manifest entry (`Sample.h`), the
  search for a missing one (`Search.h`) and the per-machine settings file (`SettingsFile.h`).
  M2 sub-project 2 (§23 #68) added the memory cap's accounting (`MemoryBudget.h`), the
  process-wide sample pool (`SamplePool.h`), float32 WAV bytes and their decoding from memory
  (`WavBytes.h`, `decodeAudioBytes` in `Decode.h`), a ring's audio saved and restored
  (`RingAudio.h`) and collect and save (`Collect.h`), with no new dependency.
  The plugin's loaders and `gg-render` share it. It links `gg-core` for the manifest entry and
  the settings' XML; `gg-core` never links it. It has no JUCE.
- **The plugin builds in two layers.** `gg-plugin-core` is the CLAP audio side and the factory,
  with no JUCE. `gg-plugin-editor` adds the JUCE editor through `clap_juce_shim`.
  - The `GG_BUILD_EDITOR` option (default ON) builds the editor and the clap-wrapper formats.
  - With it OFF, the plugin core builds headless, with no gui extension, and the `[plugin]` tests
    drive it through the CLAP factory. Nothing is downloaded.
  - The `asan` and `rtsan` presets build that way, so `process`, `params.flush` and the dry/wet
    sum run under ASan/UBSan and RealtimeSanitizer.
- **One CLAP factory exposes both plugin descriptors.** Built on free-audio/clap 1.2.10 and
  clap-helpers.
- **clap-wrapper generates VST3, AUv2 and the standalone app.**
  - **v0.17 or newer is a hard minimum**: before that, an AU effect that takes MIDI silently
    became an instrument.
  - Pinned at `4585bc6` on `next` (2026-10-28, thirty commits after v0.17.0; `4e7afc3`, fifteen
    after, until §23 #90), which carries the AUv2 transport fixes (#613, #615) and AUv2's
    values-only param rescan that no release tag has yet; move to a tag once one includes them.
  - Always list `PLUGIN_FORMATS` explicitly, because the default includes AAX.
  - AUv3 comes later. It needs the Xcode generator, one app extension per plugin, and UIKit
    support in the GUI shim.
- **JUCE is used for the GUI only**, attached through sst-clap-helpers' `clap_juce_shim`, with
  sst-jucegui widgets.
  - **JUCE 9.0.4** (`3fb5cbc`), since M0. The M0 spike built the shim and both plugins against
    it with no source changes, and on macOS arm64 every validator passed
    (`docs/spikes/m0-juce9.md`, §23 #24). CI run 40445561691 then passed on every leg, Windows
    ClangCL included, with JUCE's WebP support off (`JUCE_USE_WEBP=0`; §23 #29). M1's editor
    and sst-jucegui run on it (§23 #58); 8.0.16-security, Shortcircuit XT's version, is no
    longer a fallback.
  - JUCE 9 still has no native CLAP support, which confirms the CLAP-first choice.
- **Wanted from clap-wrapper upstream:** AUv2 tail support (its AUv2 hard-codes
  `SupportsTail()` false, so AU hosts never see the plugins' tail; §18), AUv2 MPE support, VST3
  process-context requirements, a monotonic steady time, and passing AU offline-render state
  through. The project files no reports or contributions upstream (the director, 2026-10-28;
  §23 #50): each wait is noted in `docs/m2-carry-over.md` and stays open until upstream changes.

**Dependencies.** Pin exact SHAs. The sst libraries have no tagged releases. Rows without a pin
in the bullets below (sst-waveshapers, signalsmith-stretch, MTS-ESP and
tuning-library) are planned, for M3 and later (M2 added none); they are pinned when they are
added. libFLAC, once
planned for encoding embedded captures, is not needed: M2 sub-project 2 embeds float32 WAV bytes,
which restore bit for bit and need no encoder (§18, §23 #68).

| Library | License | Use |
|---|---|---|
| free-audio/clap, clap-helpers, clap-wrapper | MIT | Plugin API and format wrappers |
| VST3 SDK / AudioUnitSDK | MIT / Apache-2.0 | Fetched by clap-wrapper |
| RtAudio 6.0.1 | MIT-style (adds a non-binding request to send changes upstream) | Fetched by clap-wrapper (CPM tag `6.0.1`, so pinned through clap-wrapper's SHA) for a Linux standalone built without the PipeWire and JACK headers, and on Windows for the standalone's ASIO® only (ASIO is a registered trademark of Steinberg Media Technologies GmbH). Its bundled Steinberg ASIO files have no license grant and are never compiled or on an include path: on Windows it is built with no API of its own, plus its ASIO backend compiled against the Steinberg ASIO SDK below (§23 #30, #56) |
| Steinberg ASIO SDK 2.3.4 (bundled in sst-plugininfra, `libs/asiosdk`) | Used under its GPLv3 option: `common/` files are "Steinberg ASIO License or GPLv3" (its `LICENSE.txt`); the `host/` files carry a BSD-3-Clause-style license | Windows standalone only: `common/asio.cpp`, `host/asiodrivers.cpp` and `host/pc/asiolist.cpp`, unmodified, compiled into RtAudio for its ASIO backend (§23 #56). A distributed Windows build carries the SDK's `LICENSE.txt` with the GPLv3 text, the `host/` files' BSD notice and the trademark attribution (`THIRD_PARTY_NOTICES.md`). The ASIO logo is not used; the plain word "ASIO" in clap-wrapper's audio settings (the API's name and its "ASIO Control Panel..." button) is a descriptive reference to the driver type (the director, 2026-10-29) |
| RtMidi 6.0.0 | MIT-style (the same terms) | Fetched by clap-wrapper (CPM tag `6.0.0`) only with `CLAP_WRAPPER_STANDALONE_RTAUDIO`, which we leave off |
| wil (Windows Implementation Libraries) | MIT | Fetched by clap-wrapper (CPM tag `v1.0.240803.1`) for the Windows standalone |
| JUCE (GUI modules only) | AGPLv3 / commercial | Editor, widgets, accessibility |
| sst-basic-blocks | GPL-3.0-or-later | SIMD setup, `ParamMetaData` and the patched Reverb 2's helpers (lag, block interpolators, quadrature oscillator, block ops); the LFOs, envelopes and mod matrix are in-house (§14) |
| sst-filters, sst-waveshapers, sst-effects | GPL-3.0-or-later | Filters (`CytomicSVF`), reverb (Reverb 2); drive later. The delay is in-house (§14) |
| sst-cpputils, sst-plugininfra, sst-clap-helpers, sst-jucegui | MIT | Real-time utilities, versioned patch state, JUCE shim, widgets |
| tinyxml (bundled in sst-plugininfra) | zlib | The patch state's XML, private to `gg-core` |
| simde | MIT | SSE intrinsics on ARM/NEON |
| dr_libs (dr_wav / dr_flac / dr_mp3) | Unlicense / MIT-0 | Audio file decoding: `gg-render`'s WAVs, and `gg-media`'s WAV, AIFF, FLAC and MP3 samples |
| r8brain-free-src 7.6 (tag `7.6`, `cb2abb9`) | MIT | Sample-rate conversion on load, in `gg-media` (`CDSPResampler24`; header-only, with its bundled Ooura FFT), since M1 sub-project 3 |
| utf8proc 2.12.0 (tag `v2.12.0`, `79cdd5a`; Unicode 18) | MIT (its Unicode data: the Unicode data license, permissive) | Unicode normalization in `gg-media`'s search for a missing sample, so a name saved in NFC is found under its NFD spelling and back; one C file, a static library linked privately into `gg-media`, since the M1 burn-down (§23 #55) |
| pffft | BSD-like | FFT: the metrics' spectra and spectrograms since M0 (§23 #21); since M2 sub-project 4 also the engine's Spectral Smear STFT (`SpectralSource.cpp` alone includes it; §23 #69) |
| signalsmith-stretch | MIT | Transparent pitch/formant shift in the FX only (not extreme stretch) |
| MTS-ESP client, tuning-library | 0BSD-style / MIT | Microtuning |
| fmt, Catch2 | MIT / BSL-1.0 | Formatting, tests |
| stb_image_write | public domain / MIT | Spectrogram PNGs |
| nlohmann/json | MIT | Metrics and render-spec JSON |
| CLI11 | BSD-3-Clause | Render CLI (`gg-render`) |

- **sst-plugininfra is in use since M1** (`main` @ `8e4271c`; it has no release tags). Only its
  `patchbase` interface target and its bundled tinyxml are built, linked privately into `gg-core`
  (ruling M1-l), so no other target sees tinyxml or its `-fno-char8_t` flag.
  - Its bundled `libs/asiosdk` is ASIO SDK 2.3.4, whose `LICENSE.txt` grants the "Steinberg ASIO
    License or GPLv3". Since 2026-10-28 three of its files are compiled, on Windows only, into
    RtAudio for the standalone's ASIO, under the GPLv3 option (§23 #56). No sst-plugininfra
    target builds any of it.
- **sst-jucegui is in use since M1 sub-project 5** (§23 #58), pinned at `22a89ca`
  (`22a89ca4e54179cbda0b8164e54e06dae5f30490`, shortcircuit-xt's pin; it has no release tags).
  It is MIT, and so are the md4c 0.5.2 and CMakeRC 2.0.0 it vendors and the flowbite-icons
  glyphs in its resources (`THIRD_PARTY_NOTICES.md`).
  - It is built only with the editor, after the shim, with its examples off, and `gg-ui` links
    it `PRIVATE`; its include directories, and CMakeRC's, are SYSTEM.
  - **JUCE compiles once, in the shim** (ruling PU-T7a). Its `sst-jucegui-juce-requirements`
    would link `clap_juce_shim_requirements`, which outside a unity build compiles JUCE's
    modules into every target that links it. `libs/CMakeLists.txt` re-points it at the
    headers-only `gg-juce-headers` plus `$<LINK_ONLY:clap_juce_shim>`, so the JUCE object code
    stays in the static shim and the link edge to it remains, and the configure fails if the
    wiring it replaces ever changes.
- **sst-effects and sst-filters are in use since M1 sub-project 4** (§23 #50), pinned at
  `987b39d` and `87a171b` (neither has release tags). They are shortcircuit-xt's pins, which pair
  with our sst-basic-blocks `9cee763`.
  - Both are header-only `INTERFACE` targets, linked `PRIVATE` into `gg-core`. Only its internal
    `core/src/space/` headers include them, so they compile in gg-core's own sources (and the
    tests that reach `core/src`) with `-ffp-contract=off`. Their tests and examples are off, and
    so is Nimbus's eurorack library.
  - `libs/CMakeLists.txt` adds sst-effects before sst-plugininfra, and fails configuring if it
    finds sst-effects' preset library, which sst-effects builds (with tinyxml) once
    sst-plugininfra's tinyxml target exists. We don't use it.
- **`CytomicSVF` is in use since M2 sub-project 4** (§23 #69), the first sst-filters unit: the
  bus filters (`core/src/fx/BusFilter.cpp`, 12 dB per octave, `setCoeffForBlock<32>` and
  `processBlockStep` doing the grid's coefficients and the per-sample interpolation). It
  compiles in `gg-core`'s own sources under `-ffp-contract=off`, `PRIVATE` like Reverb 2, and
  `Processor.h` holds a pointer to the internal unit, never an sst-filters header. Its
  coefficient path is plain arithmetic (`fasttan`), so the filtered audio is expected bit-exact
  across platforms.
- **The patched Reverb 2** (`core/src/space/Reverb2Patched.h`; §23 #50). A copy of sst-effects'
  `include/sst/effects/Reverb2.h` at `987b39d`, in the namespace `gg::core::space::patched`,
  under its GPL-3.0-or-later header plus a note that it was modified for Giga Grains. Every change
  is marked `// GG PATCH`. sst-effects itself stays an unmodified pinned submodule. Its additions:
  - **Infinite decay** (`setInfinite`; S7): the loop's decay multiplier is exactly 1.0f; the LF
    dampers run at a 0.5 Hz DC blocker's coefficient (ruling R-T8a); the HF damping
    coefficient's lower clamp is 0, not 0.01, so Damping 0 is lossless apart from DC; and
    `loopClip` bounds each block's signal after the dampers (§14, Space). The switch glides over
    30 ms, whole 32-sample blocks (ruling R-T8b).
  - **The smoothed pre-delay** (`setPredelaySamples`; S8): a fractional read, interpolated
    linearly, whose delay moves toward each new target at no more than `kPredelayMaxRate` = 0.5
    samples per sample (ruling P1), clamped to [0, 0.5 s × sr]. Once called, it replaces
    Reverb 2's own `rev2_predelay` until the next clear. Before any block has run since a clear,
    the target applies at once (ruling R-T2a).
  - **`clear()` and `clearStep(maxFloats)`:** back to a freshly constructed instance, zeroing
    only what a later read can reach (ruling R-T2b), all at once or at most `maxFloats` per call
    (ruling R-T5a).
  - With none of the additions used, it renders bit-identically to upstream `Reverb2`; a test
    pins it.
  - The patch stays ours. It isn't offered upstream (the director's policy, §23 #50).
- **Factory content** (M2 sub-project 5, group 1; §23 #70; the spec is `docs/superpowers/specs/2026-10-30-m2-presets-design.md`).
  The CC0 samples live outside this repository, in https://github.com/sethbc/giga-grains-content,
  and ship as a release asset, which meets §12's "kept out of git history". Release `v2` (group
  2; §23 #70) is `giga-grains-content-v2.zip`: 51 FLAC files in seven categories, `PROVENANCE.md`,
  the CC0 `LICENSE`, a `README.md`; 48,200,259 bytes (46.0 MiB). **v2 is a strict superset of
  v1** (release `v1`, 37 files, about 32 MiB, stays published and untouched): v1's files are
  byte-identical at the same paths, so nothing that names a v1 path breaks. The 14 files v2 adds
  are the orchestral **strings and brass** v1 lacked, from VSCO 2: Community Edition (CC0),
  beside VCSL; their provenance is as full as v1's (each file's source URL pins a commit). VSCO
  2: CE's readme asks for credit and that the samples not be sold directly; neither binds under
  CC0, `PROVENANCE.md` credits the project, and the request is recorded here for the day the
  content is bundled in something sold. Files are named by their measured sounding pitch (C4 is
  middle C), not by VSCO's own names. `content/pin.txt` pins its tag, URL and SHA-256.
  - `scripts/fetch-content.sh <dir>` downloads it, checks the SHA-256 (a mismatch is deleted),
    unpacks it to `<dir>/content/` and writes `.gg-pin` last; a folder already at the pin costs no
    network. Configure downloads nothing: the `gg-content` target runs the fetch, from the
    `content_fixture` test or `gg-products`, into `build/<preset>/content-cache/`, unless
    `-DGG_CONTENT_DIR` names a folder.
  - CI caches the fetched folder, keyed by `content/pin.txt` and the fetch script alone (the
    content is the same on every leg), restored on every run and saved on `main` only, like the
    SDK cache.
  - `gg-presets` packs the preset sources under `content/presets/` with `gg-render
    --pack-preset`; `gg-products` stages them with the samples as the installers' Content
    component (macOS, Windows and Linux; the PKG's is deselectable). `CC0-1.0.txt` joins every
    platform's license set, and `THIRD_PARTY_NOTICES.md` has a "Factory content" section naming
    the pinned tag and pointing at `PROVENANCE.md`.
  - **clap-wrapper's preset bridges** are used as pinned (`4e7afc3`, `v0.17.0-15`, then; the
    bump to `4585bc6`, §23 #90, leaves them as they were), unchanged:
    `Clap::PresetIndex` crawls the provider's locations once per library and plugin id on a
    background thread, sorted by (location kind, location, load key); the VST3 wrapper publishes
    the index as a program list on the root unit, with a `kIsProgramChange` selector parameter;
    the AU wrapper publishes it as `kAudioUnitProperty_FactoryPresets` (waiting up to 1 s for the
    crawl) and loads one through `NewFactoryPresetSet`. Both use `preset-load`. The consequences
    accepted: file locations sort before plugin locations, so INIT is last in a host's list (the
    browser lists it first); within the factory folder the order is the file names'; the
    standalone has no host list and uses the browser.
- **Study, don't depend on:**
  - Beads (MIT; Mutable Instruments published the source on 2026-10-12) and Clouds;
  - Shortcircuit XT's MIT sample-reading kernels and missing-sample model;
  - Csound partikkel;
  - EmissionControl2.
- **Avoid:**
  - Rubber Band (redundant: the grain engine covers extreme stretch and signalsmith-stretch covers
    transparent pitch shift);
  - Argotlunar (GPLv2-only);
  - NessStretch code (no license). Implement from its paper instead.
- **Written in-house:** the scheduler, grain pool, kernels and buffers, the mod matrix and its
  sources, the global delay and limiter, and the Spectral Smear (its STFT on pffft). This is the
  core of the project.
  - SHA-256: in-house (FIPS 180-4, `gg/media/Sha256.h`), tested against the standard's vectors. It
    fingerprints sample files (§18), so it needs no dependency.
- **Queues:**
  - Parameters and commands going from the UI to the audio thread use a lossless,
    non-overwriting SPSC queue, gg-core's own `SpscQueue` (no third-party queue library).
    - **An as-built exception to that rule** (M1 sub-project 5; §23 #58; confirmed by the
      director 2026-10-29): the editor's parameter changes wait in an `SpscQueue` of 1,024 that
      never overwrites, drained by every `process()` and `params.flush`, but a change made while
      all 1,024 wait is dropped. That happens only when a host ignores `request_flush` (and
      doesn't process) for more than 1,024 changes. A dropped gesture begin or end leaves the
      host an unbalanced gesture.
  - Telemetry going from the audio thread to the UI may use a lossy queue that drops entries when
    full, such as sst-cpputils' ring buffer. The M1 grain queue is gg-core's own `SpscQueue`
    (§17).

**Threading**

- **One audio thread per instance.** Hosts already parallelize across tracks.
- **Rendering is split into tasks** (built in M2 sub-project 1, 2026-10-30; §23 #64, #65; M1
  rendered its one layer on the audio thread). Per 256-sample piece, one task per rendering
  layer, each the layer's whole piece: its ring input, its grains summed in spawn order into its
  private buffer exactly as M1 summed them, its diffuser, Level, soft limiter and Feedback
  history (§14, Layers and the chain). After the join the layers are summed in order A to D.
  - **The runner** (`gg/engine/TaskRunner.h`, framework-free): `run(count, fn, ctx)` runs every
    task and returns once all have. `SerialRunner` loops on the calling thread. The plugin's
    runner calls the host's `clap_host_thread_pool.request_exec(count)` when the host offers the
    extension, and loops when it declines or doesn't offer it (CLAP's own fallback rule); the
    host calls back into `clap_plugin_thread_pool.exec(task)`, which renders that task's layer.
    A piece with one rendering layer renders it inline, with no request. clap-wrapper's VST3 and
    AU never offer the pool, so their hosts loop.
  - **Bit-identical with any runner.** Every input to the tasks (the piece's bounds and input,
    which layers render, the morph weights, computed serially into a per-piece buffer first, the
    ramps and the parameters) is fixed before the request and read-only during it. A task
    touches only its own layer's state, never calls the host, allocates, locks or draws from a
    stateful RNG, and sets `ScopedDenormalsOff` on its own thread for its duration, since a pool
    thread's floating-point mode isn't the audio thread's. So the output is the same bytes with
    `SerialRunner`, any pool size, or a host that declines (§19).
  - **Grain events are buffered per layer.** The engine's sink runs on the task's thread, so each
    layer records its piece's spawns into a fixed buffer of 2 × capacity during the task; after
    the join they are drained in layer order into the external sink (`gg-render`'s log) and the
    UI's grain queue, whose single producer is the audio thread. A full buffer drops and counts
    the event, never the audio; each layer counts its own, and `droppedGrainEvents` sums them
    (ruling LS-T11a).
  - Chunking a layer's grains into further tasks would re-associate the float sum and move every
    M1 golden once, for a gain only a one-layer instance in a host with a pool would see, so it
    is a §22 option, not M2's (M2 program design §7.4; §23 #64). Which hosts besides Bitwig
    accept the requests under load is the close's survey (§22).
- **No plugin-owned real-time threads in v1.** Neither CLAP nor clap-wrapper can join macOS audio
  workgroups yet.

**Host robustness**

- Handle 0- and 1-sample blocks. Never rely on the declared minimum frame count.
- Keep `params.flush` trivial. The AUv2 wrapper renders silence if its lock is contended. So it
  adopts no ring (M2 sub-project 2; ruling SP-T7a, §23 #68): an offer that carries a ring (a
  resize or a restore) is taken whole in a flush, its values, source and ring kept as one unit,
  and applied at the next `process()` call's start, a frozen restore held from that call's
  first sample.
- Keep an internal monotonic sample clock and detect loops and jumps from discontinuities, rather
  than trusting host loop flags.
- Accept MPE both as CLAP note expressions (Bitwig) and as the MIDI-MPE dialect (Ableton and
  others, via VST3 and AU). Both feed one internal per-voice modulation path.

### 16. Performance targets

*The headline was measured on the reference core at the end of M1 and is published as measured
(§23 #39, #60). The M0 kernel benchmark (below) was its starting point.*

- **Headline (measured on the reference core, published as measured):** **2,000 Normal grains
  per instance at 48 kHz in 19.5% of one Apple M5 Max performance core** (X = 19.5, at block
  256; 23.4% at block 32 and 21.2% at block 64).
  - **M2's close re-measured it** (sub-project 7, 2026-11-03, idle, `ca60f54`;
    `docs/benchmarks/2026-11-03-m2-headline.json`, whose README section has the conditions):
    `engine-sample` 2.431 / 2.188 / 2.017 ns, **23.3% / 21.0% / 19.4%** at blocks 32 / 64 / 256,
    ratios to `reference` 1.1704 / 1.0897 / 1.0302 against M1's 1.154 / 1.072 / 1.024 (+1.4% /
    +1.6% / +0.6%, inside ruling R3's 3%). The headline above is M1's measurement and stands as
    published; at M2's close X is 19.4%. Beside it, from the same run: `engine-live` 32.6% /
    26.1% / 20.9% (3.399 / 2.717 / 2.177 ns), `engine-sample-eco` 16.9% / 14.9% / 12.9%,
    `engine-live-long` 33.8% / 27.4% / 22.3%, `engine-layers` 29.4% / 27.1% / 22.9%,
    `engine-layers-full` (8,000 grains) 139.5% / 120.0% / 90.9%, `engine-sample-hq` 160.3% /
    155.9% / 152.6%, `engine-sample-hq-up` 162.6% / 157.7% / 153.4%, `engine-overload` 23.3% /
    21.2% / 19.7%, `engine-sample-smear` 23.3% / 21.0% / 19.4%, `engine-buses` 23.4% / 21.1% /
    19.4% and `engine-spectral` 32.6% / 26.1% / 21.0%.
  - X was measured at the end of M1, in sub-project 6, on 2026-10-29 at `be2291e` (main's
    engine at M1's close; §23 #60): `gg-bench`'s `engine-sample` case at 2.44 / 2.21 / 2.03 ns
    per stereo grain-sample at blocks 32 / 64 / 256, the `release` preset, AppleClang 21, five
    interleaved rounds of 2 s, nothing else running (the one-minute load average was 2.58
    before the run). The run's JSON is `docs/benchmarks/2026-10-29-m1-headline.json`; its README
    has the conditions and the comparison with sub-project 2's figures. **There is no ceiling
    on X.**
  - The text always names the chip, so nobody reads it as an M1 or x86 figure.
  - The M2 budget manager keeps any grain count safe by degrading deterministically (§14). The
    promise is an honest, reproducible figure, not a threshold.
  - `gg-bench` also times the same case over the live ring; that is published beside X as the
    FX's figure (§23 #44): **31.9% / 25.8% / 21.0%** at blocks 32 / 64 / 256 (3.32 / 2.69 /
    2.18 ns), from the same run. The Eco tier, engine-only until M2, measured 16.6% / 14.6% /
    12.8%.
  - **Two four-layer cases are published beside X, report-only** (since M2 sub-project 1;
    §23 #65): `engine-layers`, four `engine-sample` engines (layers 0 to 3) with 500 grains each,
    rendered through `SerialRunner` and summed in layer order, what a host without a thread pool
    pays for a four-layer instance at X's grain count; and `engine-layers-full`, 2,000 grains in
    each, 8,000 in all. Both are in the default case list, so CI's legs time them; their ratios
    have entries on the x86 legs since the AVX2 task (§23 #66), none on `mac-universal`, and
    their reference-core figures come from sub-project 7's idle run. X's case and definition are
    unchanged.
  - **Four more cases are published beside X, report-only** (since M2 sub-project 3; §23 #66):
    `engine-sample-hq` (X at the HQ tier), `engine-sample-hq-up` (the same at Pitch +19, rate
    2.997, each grain reading mip level 1 at residual 1.5: the pyramid path) and
    `engine-overload` (one engine at capacity 16,384 and a quota of 2,000 Normal grains' worth,
    1 s grains asked for at four times that rate, so the drop and steal rungs run steadily: ns
    per rendered grain-sample, the ladder's bookkeeping over rendering), and
    `engine-sample-sse` (X with the 4-wide kernels forced, the fallback's figure). An HQ grain
    costs 6.79 Normal ones (the median of three `--quick` runs at block 256 on the M5 Max; HQ's
    weight in §14). X's definition is unchanged.
  - **Sub-project 3's published figures** (report-only; Task 14's run on 2026-10-30 at `ab0bbe5`,
    the `release` preset, the Apple M5 Max, AppleClang 21, 4-wide kernels, five interleaved
    rounds of 2 s; the one-minute load average was 3.4 before the run and 1.8 after), ns per
    stereo grain-sample at blocks 32 / 64 / 256, with the percent of one core at 48 kHz:
    - **`engine-sample-hq`:** 16.87 / 16.47 / 16.04 ns, **162.0% / 158.2% / 154.0%** (2,000
      HQ grains do not fit one core: 8.0 times `reference`);
    - **`engine-sample-hq-up`** (Pitch +19, level 1): 17.07 / 16.57 / 16.14 ns, **163.9% /
      159.1% / 155.0%**;
    - **`engine-overload`** (ns per rendered grain-sample): 2.41 / 2.19 / 2.04 ns, **23.4% /
      21.2% / 19.7%**;
    - **X again, at this commit:** `engine-sample` 2.47 / 2.23 / 2.06 ns (23.7% / 21.4% /
      19.8%), **within 3% of the headline's 2.44 / 2.21 / 2.03: +1.4% / +0.8% / +1.3%**. The
      headline stays 19.5%. `engine-sample-sse` (4-wide, as `engine-sample` is on arm64)
      measured 2.44 / 2.21 / 2.04 ns. The run's JSON and text are in the session record, not
      published as a baseline: the M5 Max is not a CI model.
    - The first idle run of this sub-project, at `dd27cd4`, found X **+6.3% / +5.5% / +3.8%**:
      outside the bar, and fixed (§23 #66, ruling ES-F2). The figures above are the fixed
      code's.
    - HQ against X in this run: 6.8 / 7.4 / 7.8 times at blocks 32 / 64 / 256, against the
      weight's 6.79 (from `--quick` runs on a loaded machine). The weight stays provisional
      until M5's calibration (§14). **The close's idle run** (`ca60f54`) measured 6.87 / 7.42 /
      7.88 (`engine-sample-hq` 16.688 / 16.237 / 15.887 ns over `engine-sample`), the same within
      0.1; the weights are unchanged.
  - **The AVX2 measurement** (`docs/spikes/m2-avx2.md`; §22): on both x86 floor legs
    `engine-sample`, dispatched to the 8-wide kernels, beat `engine-sample-sse` in the same
    process and rounds by 13% to 35% of the SSE ratio in all 18 leg-run-block cells of three
    full-length runs, clearing the runs' spread (the range of the per-run improvement) by 2.5 to
    13 times. The x86 baselines were remade from the same runs, and the six report-only cases now
    have entries beside the four gated ones on those legs (not on `mac-universal`); the gate
    stays report-only. `gg-bench` prints the table it used (`kernels:`) and its JSON records
    `kernelLanes` (8 or 4). `--grains` is bounded by `kMaxGrainCapacity`.
  - **A long-ring case is published beside X, report-only** (since M2 sub-project 2; §23 #68):
    `engine-live-long`, `engine-live`'s stream over a 300 s ring instead of 16 s, Position 150 s
    and Spray 300 s so the reads span the ring, warmed up for 300 s of audio so the ring is full:
    the cache cost of a minutes-long buffer. It is in the default case list, its ratios "not in
    the baseline" until three full CI runs are merged in at the close; each of its three
    configurations holds a 115 MB ring, about 216 MB with its levels. Under `--quick`, a smoke check with no verdict, every
    engine case's warm-up is capped at 16 s, the longest any other case uses, so the other cases'
    quick runs are unchanged and `engine-live-long`'s reads a partly filled ring (ruling SP-T9a).
    `engine-live`'s figure with the ring pyramid's levels (landed in sub-project 2's Task 12, §23
    #68) is measured at sub-project 7's idle run: `engine-live` 3.399 / 2.717 / 2.177 ns
    (32.6% / 26.1% / 20.9%) and `engine-live-long` 3.524 / 2.851 / 2.323 ns (33.8% / 27.4% /
    22.3%), ratios 1.6960 / 1.4143 / 1.1866 (`docs/benchmarks/2026-11-03-m2-headline.json`).
    `engine-sample`, X, is untouched by sub-project 2.
  - **Sub-project 4's cases are published beside X, report-only** (§23 #69), in the default case
    list so CI's legs time them, their ratios "not in the baseline" until three full runs add
    their entries: `engine-sample-smear` (`engine-sample` at Smear 1: two extra draws a spawn
    and the jitter's reads), `engine-buses` (half the grains on bus 2) and `engine-spectral`
    (X's stream over the Spectral kind, the STFT running on the same noise as material).
    Idle run of 2026-11-01 (`docs/benchmarks/2026-11-01-m2-extremes.json`, commit `cc68e3d`),
    medians, ns per stereo grain-sample at blocks 32 / 64 / 256, with the ratio to `reference`:
    `engine-sample-smear` 2.500 / 2.265 / 2.100 ns, 1.176 / 1.099 / 1.035; `engine-buses` 2.505 /
    2.255 / 2.073 ns, 1.171 / 1.091 / 1.031; `engine-spectral` 3.403 / 2.721 / 2.212 ns, 1.604 /
    1.328 / 1.108 (the STFT's hops on top of X). X itself, 2.472 / 2.245 / 2.065 ns (23.7% /
    21.6% / 19.8% of one M5 Max core), shows no regressed ratio: 1.172 / 1.101 / 1.032 against the
    M1 headline's 1.154 / 1.072 / 1.024 is +1.6% / +2.7% / +0.8%, inside ruling R3's 3%. To
    separate the extremes from three sub-projects' drift, main (`4fed5bb`) and this head were
    timed alternately in the same window (`2026-11-01-m2-extremes-ab-main.json`, `-ab-head.json`):
    main's 1.1770 / 1.0896 / 1.0338 against the head's 1.1746 / 1.0939 / 1.0327 and 1.1770 /
    1.0934 / 1.0322, so the extremes move X by at most ±0.4% at any block size.
  - **`gg-bench --chain`** times the wet chain's new per-sample stages (two bus filters, the
    delay and the limiter at Peak) on stereo noise at 48 kHz, as `--space` times the diffuser
    and reverb: report-only, its own JSON. At the same window
    (`docs/benchmarks/2026-11-01-m2-extremes-chain.json`): 11.781 ns per stereo sample (min
    11.773, IQR 0.009), **0.057%** of one M5 Max core. The close's idle run
    (`docs/benchmarks/2026-11-03-m2-chain.json`, `ca60f54`): 11.706 ns (min 11.656, IQR 0.049),
    **0.056%**; and the new `spectral` stage, one Spectral Smear's STFT with no grain reading it,
    **26.705 ns, 0.128%** of one core, which is what a Spectral layer costs while switched off
    (the STFT keeps running; carry-over row 372, closed as measured).
  - **Space's cost is published beside X too** (`gg-bench --space`, report-only, with no ratio or
    baseline; §23 #50): the diffuser and the reverb on stereo noise at 48 kHz, per stereo sample.
    Sub-project 4's fix wave measured 28.49 ns, 0.137% of one M5 Max core, at its final code
    (`ac81dfb`; §23 #50 has the details), and sub-project 6's idle run 29.44 ns, 0.141%
    (`docs/benchmarks/2026-10-29-m1-space.json`; §23 #60), and the close's idle run 28.718 ns,
    **0.138%** (`docs/benchmarks/2026-11-03-m2-space.json`); X itself, grains only, is unchanged
    by it.
- **X is measured on the real engine's render path,** not on M0's prototype kernels in
  `tools/gg-bench/Kernels.h`.
  - The case: 2,000 Normal-quality grains of 100 ms, at 48 kHz, through `Engine::process`,
    scheduler and pan included, reading a stereo sample buffer in RAM (Sample mode; §23 #44).
  - The method is gg-bench's: the median of interleaved runs, performance-core QoS, the `release`
    preset, an idle machine.
  - The published number records the commit SHA and the block sizes (32, 64, 256).
- **Reference hardware:** one performance core of the director's Apple M5 Max, on bare metal.
- **Measured kernel cost** (M0 benchmark on the director's M5 Max; details in
  `docs/spikes/m0-kernel-benchmark.md`). Costs are per stereo grain-sample, for 2,000 grains of
  100 ms at 48 kHz, at block sizes 32 / 64 / 256:
  - **Hermite, 4-wide SIMD:** 2.25 / 2.15 / 2.07 ns, which is 21.6% / 20.7% / 19.9% of one core.
  - **Linear, 4-wide SIMD:** 1.49 / 1.39 / 1.35 ns (14.3% / 13.3% / 13.0%).
  - **16-tap windowed sinc, scalar:** 8.75 / 8.68 / 8.61 ns (84.0% / 83.4% / 82.6%).

  These replace the first-principles estimates: 1 to 1.5 ns for linear, about 2 ns for Hermite
  and 7 to 13 ns for sinc.
  - The measurements land inside those estimates, or up to 12% above for Hermite.
  - The 2× to 4× penalty feared from gathers and cache misses did not appear on this machine.
- **Informative floor figures** (M0 CI, full-length `gg-bench`). CI's legs run the
  full-length benchmark on the nightly schedule, on a manual dispatch and on a release tag (a
  pull request and a push to `main` run `--quick --rounds 1`, a smoke check with no verdict; §23
  #57, #73, #75), and their figures go in
  the benchmark doc as informative, not promised.
  The SIMD Hermite kernel, 2,000 grains, at blocks 32 / 64 / 256:
  - **Apple M1 VM** (GitHub `macos-latest`): 5.52 / 5.41 / 5.00 ns, which is 52.9% / 51.9% /
    48.0% of one core.
  - **AMD EPYC 7763, Windows** (ClangCL): 6.22 / 5.93 / 5.72 ns (59.7% / 56.9% / 54.9%).
  - **AMD EPYC 7763, Linux** (gcc 12): 7.21 / 6.62 / 6.26 ns (69.2% / 63.5% / 60.1%).

  That is 2.4× to 3.2× the M5 Max's share for the same grains, which §21 rounds to 2× to 3×. Small blocks also cost
  relatively more on some legs. On Linux, block 32 costs 15% more per grain-sample than block
  256, against 8% on the M5 Max.
- **CI's CPU gate is a regression gate,** measured as ratios within one process (carry-over
  B-I2). It never gates on an absolute headline. It reports without failing through M1 and the
  0.x releases, and turns blocking at 1.0.0, after M5, with the published X (§23 #60, #61;
  carry-over rows 144 and 148).
- **Sub-project 2's optimization work was a fixed list of four levers,** each tried, kept if it
  won, then stop (§23 #39, #46):

  | Lever | What it attacks |
  |---|---|
  | Per-grain software prefetch | the first, likely cache-missing taps of each grain in each block |
  | A stereo-interleaved source layout | two address streams per tap; it halves the per-tap address work |
  | Cache-aware grain scheduling | source locality across grains. It reorders memory fetches only, never the summation |
  | Contiguous vector loads with shuffles in place of the per-lane gather | 8 one-at-a-time tap loads per output sample (NEON `tbl`, SSE shuffles) |

  - **A lever was kept** when its in-process ratio against the current kernel improves beyond the
    run-to-run spread, on the M5 Max or on any CI floor leg, and no leg regresses beyond its
    spread. Judging on the M5 alone would undersell the cache levers, which mainly help older
    machines.
  - **Every lever obeys the determinism rules unchanged** (§14): grains are summed in spawn
    order, so a reordering lever may change only fetch order; the output stays bit-identical at
    any block size; a lever that changes output bits justifies it in a golden-regeneration
    commit.
  - **When all four are tried, optimization stops.** Sub-project 6 measured X and published it
    (above; §23 #60).
  - **Outcome (sub-project 2, 2026-10-27): one lever kept, three rejected, optimization
    stopped.** The stereo-interleaved source is kept, both the sample source and the live ring:
    on the M5 Max it makes the Sample case 7–9% faster and the live case 9–13% faster. Its
    sample half is kept by the director's exception to the keep rule (§23 #46). Prefetch,
    cache-aware scheduling and shuffles are rejected, prefetch and shuffles after a second trial
    ported onto the interleaved kernel. CI ratios drift between runs by more than their in-run
    spread (§23 #47). The tables and reasons are in `docs/spikes/m1-cpu-levers.md`.
- **UI budget:** at most 3 ms of UI-thread time per frame with 8,000 visible grains.
  - **Measured in M0** (`docs/spikes/m0-canvas-budget.md`): with 8,192 grains, the editor's
    "frame ms" stayed under 3 ms in Ableton Live and Bitwig Studio on the M5 Max (§23 #26).
  - On macOS that readout is UI-thread time only, and so a lower bound on the whole frame.
  - **Checked since sub-project 5** (§23 #58, decision D3): the densest setting a host can reach
    stays at or under 3 ms in Live and Bitwig, and an idle `gg-bench --canvas` run at 8,192
    grains at or under 3 ms, taken with sub-project 6's X measurement (below). In sub-project 5 a
    run with nothing building (load average about 4) measured 2.80 ms (M0's, 2.77 ms).
    **Sub-project 6's idle run measured 2.87 ms** (p95 2.92 ms;
    `docs/benchmarks/2026-10-29-m1-canvas.json`, §23 #60), inside the budget.
  - **With four lanes** (since M2 sub-project 6; §23 #71):
    `gg-bench --canvas` times the shipped path, the banded resolve with the four layers' tints,
    and its JSON says so (`"lanes": 4`, `"resolve": "banded"`); the figures above are the
    one-tint resolve's. **M2 sub-project 6's idle run at 8,192 grains:** 2.959 ms at 8,192 grains with four lanes (mean 3.117 ms, p95 4.009 ms; splat 0.83 ms, resolve 2.071 ms), at or under the 3 ms budget by its median, the exit criterion met; the mean is just over 3 ms; 4,096 grains 2.55 ms, 16,384 grains 3.751 ms; idle window 2026-10-30 19:27-19:29 CDT, the other sessions paused, load average 3.15 falling to 2.43, Apple M5 Max, at `25c7707` (`docs/benchmarks/2026-10-30-m2-views-canvas.json`).
     **The close's idle run** (2026-11-03, `ca60f54`, load average 1.63 before): 2.474 / **2.848**
    / 3.592 ms at 4,096 / 8,192 / 16,384 grains, p95 2.880 ms at 8,192, four lanes, banded
    resolve (`docs/benchmarks/2026-11-03-m2-canvas.json`): inside the 3 ms budget.
     The GPU spike read the live canvas's
    own readout at 4.10 ms, `drawImage` and the overlays included (8,192 stress grains, the
    canvas 1600 × 600 logical at 2×), under contention (load average 10.9 to 18.6, five agent
    sessions on the machine), so that is no budget figure (`docs/spikes/m2-gpu-canvas.md`). The live readout, a separate, report-only figure (the FX standalone's header frame readout, `GG_STRESS_GRAINS=8192`, a 1600 × 600 canvas in a 1624 × 762 editor at 2×, five 20 s runs, built at `25c7707` plus Task 17's readout hook in a scratch tree, not merged): ui_ms 3.616, 3.457, 3.557, 3.499 and 3.503, median 3.50 ms, with 2,943 to 3,330 frames per 20 s (about 147 to 167 a second); it includes `drawImage` and the overlays, which the headless bench doesn't.
  - **The repaint rate (D12, decided 2026-10-30).** The canvas repaints at the display's refresh
    rate, with no cap; the director chose that over a 60 Hz cap. The cost, stated plainly: the
    GPU spike measured about 65% of the message thread on a 240 Hz display under load (about
    4.1 ms a frame, contended; about 156 frames a second), and the share at 120 Hz idle is an
    estimate of about 35% (about 2.9 ms a frame at the 1600 x 600 canvas). Each open editor adds
    its own share. The budget is per frame and is not affected.
- **SIMD:** 4-wide via sst and simde, behind an internal batch API, plus 8-wide AVX2 sample and
  live kernels chosen once at load by CPUID, the same bits (§14, §23 #66); no third-party
  dispatch library.

### 17. UI and interaction

**Perform view (the default)**
- **The canvas.** Waveform lanes, one per layer. Live grains are drawn where the engine is playing
  them:
  - x = position
  - size = grain length
  - brightness = gain
  - offset = pitch (since M2 sub-project 6, the director's D1; pan is no longer drawn)
- **Direct manipulation:** drag the scan window, drag the spray edges, drag the window's top and
  bottom edges for pitch spread and its body vertically for pitch (D2, amending "drag vertically
  for pitch spread"; §23 #71), freeze, drop a file, record.
- **Eight macros**, matching the CLAP remote-controls page size: Size, Density, Pitch, Spray,
  Texture, Freeze, Feedback, Mix.
  - Each drives several parameters along curves.
  - Each ships with musical factory assignments.
- **A layer strip** for A to D, with an **XY morph** pad.

**Other views**
- **Edit:** per-layer panels for Source, Scheduler, Grain, Pitch, Space and Filter. Every grain
  parameter shows its base value, spread with distribution, and modulation depth.
- **Mod:** drag a modulation source onto a control to modulate it. Depth shows as rings around the
  control.
- **FX:** the global effects chain.

**Presets and editing**
- Tags, favorites, an INIT preset, undo/redo.
- **Mutate:** randomize with per-parameter locks and an amount control.
- **As built** (M2 sub-project 5, group 1; §23 #70; the spec §6.8, §6.9), in both products' editors:
  - **The strip** replaces the header's "Init" label, in the same `header.preset` slot (90 px):
    previous and next, and the preset name, with a trailing dot (`Glass Hold •`) when a base
    value differs from the loaded preset's (any of the 283 values, the ring parameters aside,
    since a load keeps them), which opens the browser.
  - **The browser** is an overlay on the canvas, closed by its button, Escape or a pick: a search
    field (name, description and tags), the four focus tags as toggles and the other tags as a
    list, a Favorites toggle with a star on each row, and the list (INIT first, the factory
    presets in file-name order, then the user's). A pick loads the preset and keeps the browser
    open, so the arrow keys audition down the list. "Save as…" (name, tags, description; a name
    that would overwrite asks first), "Delete" on a user preset, "Content folder…" (the
    settings' `contentFolder`), a status line ("Factory content v2 in …" or "No factory content
    found…"). A listing is capped at 5,000 files per folder, and the status line says so.
  - **Mutate** opens from a button in the browser. Its law is relative jitter in each
    parameter's CLAP domain (a stepped one draws a new step with probability Amount); the
    structural locks (Mix, Level, layer on-switches, Sources, Freeze, the macros, Morph, Reverb
    Amount and Dry to Reverb, Step Length) and every parameter of a layer that is off are locked by
    default (since group 2, 62 slots: the structural locks by section are given under "As built,
    group 2" below); Amount defaults to 15%. Each press is deterministic from the instance's seed and a
    press counter (not saved), and reaches the host as begin/value/end gestures through the UI
    queue, in batches, so a host records automation.
  - Undo and redo came with M2 sub-project 6's PR 2 (Undo and redo, under The M2 views as
    built, below).
  - **As built, group 2** (2026-11-01; §23 #70; the spec §6.9):
    - **The structural locks are 62 slots,** by section: every layer's Quality and five ring
      parameters (Ring Length, Loop, Loop Length, Overdub, Splice: capture and memory), Budget,
      Delay Amount (as Reverb Amount is) and the limiter's Mode, Ceiling and Release (output
      safety), beside the 33 above. Smear, Bus Split, the buses' filters, cutoffs and
      resonances, and the delay's Time, Feedback, Damping and Ping-pong are free to move. The
      panel gains Delay and Limiter sections after Space, and shows each layer's Ring Length,
      which no press moves, dimmed and with no toggle.
    - **The strip, the browser and the Mutate panel take the editor's theme** (carry-over row
      313, in part): each reads the editor's palette and takes it again on a theme switch. Text
      and controls are held to 4.5:1 (3:1 for the favorite star, a glyph, and the dimmed Mutate
      rows) on both grounds, checked as the editor-frame tests do. The browser's list sits on the
      header's ground (the lanes' dark is the canvas's alone).

**Basics**
- Resizable and HiDPI-ready.
- Keyboard-navigable.
- Color-blind-safe layer palette.
- Light and dark themes (M2); full theme support in M5: follow the OS, high contrast, more
  built-in themes and user theme files (§20, §23 #72).

**Canvas rendering**
- **Renderer.** A single `GrainCanvas` component behind a renderer interface.
  - v1 uses a CPU additive "splat" renderer drawing into an image. It works on every platform.
  - A GPU backend gets a timeboxed spike in M2. Candidates are Visage (which ships a CLAP example
    but is desktop-only) and native Metal/GL. The CPU renderer held the §16 UI budget in the M0
    host check, so the spike didn't move earlier (§23 #26).
  - **The M2 spike decided: the CPU renderer stays** (M2 sub-project 6,
    `docs/spikes/m2-gpu-canvas.md`; §23 #71). JUCE OpenGL and Metal
    were built and measured on the branch `spike/gpu-canvas` (`082a874`, unmerged); Visage was
    not tried, since a second UI framework beside JUCE is a new platform risk by definition.
    Measured interleaved at one contended load, both beat the CPU path by 6.6× to 45× depending
    on the column, but neither clears the criterion's "no new platform risk": OpenGL is
    deprecated on macOS and went unmeasured in the hosts, under Rosetta, on Linux and on Windows;
    Metal is macOS-only, so it would be a second renderer beside the CPU one. If the budget ever
    breaks, Metal is the macOS lever, and M0's levers (resolve only the rows grains touched)
    come first.
- **Audio → UI data.**
  - One compact event per grain *spawn*, sent through a queue that drops entries when full. The UI
    extrapolates each grain's progress from an atomic engine clock.
  - Triple-buffered per-block snapshots.
  - A peak pyramid of the live buffer.
  - The engine publishes the grain queue and the snapshot only while the editor is open; the
    live peaks are written always, so an editor that opens late finds the ring's recent audio
    (ruling PU-T9b, below).
  - Visual thinning never touches the audio RNG.

**The M1 Perform view as built** (M1 sub-project 5, 2026-10-29; §23 #58). One lane, layer A's,
for both products, on sst-jucegui's controls and its built-in dark style sheet. M2 sub-project 1
is headless by choice: the view still shows layer A alone, and layers B to D are reached through
the host's generic editor, automation, the remote-controls pages and `gg-render` until
sub-project 6 (§23 #65). M2 sub-project 6 replaced the view (The M2 views as built, below); this
record stays as M1 built it, and its audio → UI and UI → host paths stand, extended by addition:
- **The layout:** a header (the product's name, the preset name (since M2 sub-project 5 the
  preset strip below; "Init" before it),
  Perform selected, Edit grayed out until M2, and the editor's frame time averaged over 60
  frames); in the instrument, the loader strip (§18); the canvas across the full width; and one
  macro row below it, the eight macros in remote-control order, Macro Freeze a toggle and the
  rest knobs. The window opens at 900 × 500 and resizes, HiDPI-aware, down to 640 × 360. A
  focused macro moves by 1/100 of its range per arrow key, 1/1000 with Shift.
- **The FX lane** is the 16 s live ring with the write head fixed at the right edge as "now":
  x is seconds behind the head, history scrolls left, and the scroll stops while frozen. **The
  instrument's lane** is static: x is the position across the loaded sample.
- **Each sounding grain is a capsule** splatted by M0's CPU `SplatRenderer` at physical-pixel
  resolution: x spans its read extent (start position to start + rate × length), so its width is
  its length; brightness is its gain, clamped to 0..1; y is its pan, hard left (−1) toward the
  top and hard right toward the bottom, until sub-project 6 frees the axis for pitch (Pitch
  Spread exists since M2 sub-project 1; §14). Under
  the grains, the FX draws the live ring's peaks and the instrument the sample's outline (peaks
  of 512 frames, computed once per sample on the main thread).
- **The scan window** sits at Position's and Spray's base values. **The FX's is one-sided,**
  `[Position, Position + Spray]` seconds behind the head, as the engine draws a live grain
  (ruling PU-T8a), so its newer edge is Position itself and drags as the body; the instrument's
  is `Position ± Spray`. A dashed **ghost window** shows the effective values (modulation and
  macros) wherever they differ. A tint marks Freeze, and a marker at the head shows Feedback
  above 0.
- **Direct manipulation edits base values as host gestures:** a drag on the body sets
  Position, on an edge Spray. Each drag begins one gesture on the press, sends a value for each
  move that changes it, and ends the gesture once, on the release wherever it lands, or from the
  canvas's destructor. The macros' knobs and toggle send gestures the same way. No other canvas
  gesture exists in M1.
- **The instrument substitutes slot 8:** Macro Level, in place of the FX's Macro Mix (§14,
  Macros). Slot 7 shows Macro Feedback in both products since M2 sub-project 1 (§23 #65; M1's
  instrument showed Macro Stereo there): in the instrument it acts on a Live or Resample layer
  and is inert on a Sample layer, the default (§23 #68).
- **Thirteen remote-controls pages** (§18; three in M1): "Perform" (the eight macros) first, then
  "Cloud" and "Space", then, since M2 sub-project 1, "Layers", "Morph" and "Cloud B" to
  "Cloud D", then, since M2 sub-project 4 (§23 #69), "FX" (the delay and the limiter) and
  "Filter A" to "Filter D" (a layer's Smear, Bus Split and two bus filters). The Edit view's
  Filter panel and the FX view have drawn these parameters since M2 sub-project 6's PR 2; before
  it they were headless.
- **Audio → UI, as built** (gg-core, JUCE-free, lock-free and allocation-free on the audio
  thread):
  - **The grain queue:** a `GrainVis` per spawned grain (onset, start position in ring time or
    source frames, rate, length, gain, pan, and since M2 sub-project 1 its layer, for
    sub-project 6's lanes), in the sink's order (piece, layer, spawn; §15), in an SPSC queue of
    4096 that drops
    the newest and counts them when the editor falls behind. The canvas keeps up to 8,192
    sounding grains, dropping the oldest.
  - **The snapshot:** a `UiSnapshot` at the end of every `process()` call (the clock, the write
    head, the freeze setting, the active grain count, Position's and Spray's base and effective
    values, and Feedback: layer A's, with every layer's grains counted since M2 sub-project 1),
    through a triple buffer (`LatestValue`), so the editor always takes the newest whole one.
  - **The clock:** the sample clock at the end of the last publishing call, an atomic stored with
    release order after its snapshot. The UI extrapolates it by wall time, never backwards within
    an activation and at most 0.25 s past its last change.
  - **The peak pyramid is one level in M1:** `LivePeaks`, max(|L|, |R|) per 64-frame bucket of
    what the ring takes, indexed by ring time (the frames the write head advanced over, so a
    freeze writes none and a grain's start position reads its own frames; ruling PU-T9a). Each
    layer has one, in both products since M2 sub-project 1, written on every render piece
    whether or not the editor is open, so an editor that opens late finds the ring's recent
    audio, never an older pass's (ruling PU-T9b).
  - **Publishing is gated** on the editor being open for the queue and the snapshot: the
    `Processor` reads the flag once per call, and with it off neither is written. The audio is
    bit-identical either way. The plugin owns one editor context for its life and points it at
    each activation's `Processor`, null while inactive (ruling PU-T10a).
- **UI → host:** the editor's gestures and values go through the plugin's 1,024-entry queue
  (§15) to the audio thread, which takes them at the start of the next `process()` or
  `params.flush`, before the host's events, applies each value as a host's would be, and sends
  `CLAP_EVENT_PARAM_GESTURE_BEGIN`, `CLAP_EVENT_PARAM_VALUE` and `CLAP_EVENT_PARAM_GESTURE_END`
  to the host's output events, so hosts record the drag as automation. Each change asks for a
  flush (`request_flush`, one outstanding); an output event the host's list refuses is counted,
  never retried.

**The M2 views as built** (M2 sub-project 6, PR 1, 2026-10-30; the spec
`docs/superpowers/specs/2026-10-30-m2-views-design.md`; the director's D1–D11,
§23 #71). Four views on sst-jucegui, in both products. PR 2 (built 2026-11-01, below)
added wave 2's parameters to the same layout table, the FX view's Delay and Limiter, the memory
readout, the sample strip for every layer, drops onto lanes B to D, and the ring's marks, loop
and capture on the lanes.
- **The frame:** the window opens at **960 × 600** (D11) and lays out down to 640 × 360,
  HiDPI-aware. The header holds the product's name (a click opens the About box), the preset
  name (`header.preset`, the browser's slot), **Perform · Edit · Mod · FX** as a radio group,
  undo and redo (their tooltips name the entry), the Dark/Light switch, a notice line (4 s) and
  the frame time. One view shows at a time; switching shows and hides, so the canvas keeps its
  grains. The plugin instance remembers the last view while it lives; the view, the selected
  layer, the theme and the undo stack are never parameters and never in the state.
- **Perform:** four lanes always (D3), equal height, one per layer, each with its header cell,
  the layer strip's: the letter in its color (a click selects the layer for Edit and Mod), On,
  Level (a horizontal slider with no label; its tooltip names it), Source and Freeze. The header
  column is chrome and follows the theme; the lanes beside it stay dark in both (D9). An off
  layer's lane is dimmed. **A lane draws
  its layer by its built kind,** in either product: a Live layer's as M1's FX lane (the ring,
  the head at the right edge), a Sample layer's as M1's instrument lane. **The vertical axis is
  pitch** (D1): a grain at rate `r` lies `12·log2(r)` semitones from the lane's center, ±48
  semitones across the lane, clamped; a reverse or non-finite rate draws at the center. One
  accumulator for the four lanes, its resolve tinted per lane. **The scan window is a
  rectangle** (D2): x from Position and Spray as M1, y from Pitch ± Pitch Spread. Its left and
  right edges drag Spray, its top and bottom edges Pitch Spread, and its body Position or Pitch,
  locked to the axis the first 4 px pick; a corner takes the Spray edge, and a thin rectangle
  keeps a body to grab. A dashed ghost rectangle shows the effective values. A double-click on
  a lane's body toggles that layer's Freeze; each lane has its own freeze tint and Feedback
  marker. A file dropped onto any lane loads into its layer (PR 2, below). **The XY pad:** A bottom-left, B bottom-right, C top-left, D top-right, Y
  up, each quadrant shaded by the morph law's weight; a drag is one gesture pair on Morph X and
  Y. The macro row is M1's. The canvas keeps up to 8,192 sounding grains per layer.
- **Edit:** the selected layer's panels (D5; PR 2 below adds Ring): Source, Scheduler, Grain, Pitch, Space, Filter
  (hidden while empty), LFO 1 and 2, Env 1 and 2, Random 1 and 2, and Step, under a compact
  strip. One table (`ui/include/gg/ui/EditLayout.h`) places every parameter slot exactly once;
  a test fails, naming the slot, on any parameter no view shows. A spread pair draws together,
  the base knob's arc showing the spread's extent. Routes show as rings on knobs and as dots on
  toggles and menus, in their source family's color.
- **Mod** (D6): the strip, 24 source chips in five families, the Edit panels in mod mode (the
  controls inert, the rings draggable for depth, a ring's click selecting its route), and the
  48-slot route list (source, source layer, target, target layer, depth, curve, clear), which
  scrolls to a route that appears in an empty slot (a drop, an undo or redo, a load) and outlines
  it. A chip
  dropped on a control adds a route in the first empty slot, a quarter of the target's range,
  Bipolar (Unipolar for a stepped or on/off target), on the selected layer; a drop whose source
  already reaches the control selects that route instead; a full matrix, a macro and a modulator's
  control refuse the drop with a notice. In Perform, the strip's controls and the macro row
  (which refuses) take a chip dragged there by switching views mid-drag; the pad takes none. The
  factory routes are ordinary rows.
- **FX** (PR 1; PR 2 below adds Delay and Limiter): Reverb (Amount, Decay, Size, Damping, Pre-delay, Modulation, and Dry to Reverb in the
  FX) and Output (Mix, in the FX, then the engine's Budget, in both products; ruling VW-T19a,
  which also puts each layer's Quality in the Grain panel after Texture, until PR 2's panel
  design). **Reverb Size sets on release** (D10): the knob shows its
  target while dragged and sends one value at the gesture's end, so moving it from the UI is one
  step, never a sweep. Automation and a host's gestures are unchanged and still click (row 217,
  closed as decided).
- **Undo and redo** (D7): one stack per plugin instance, 200 entries, kept across the editor's
  close and open. One entry per editor gesture (the pad's two parameters make one), one per
  route edit, and one whole-state entry for each Mutate press and each preset recall (sub-project
  5's). **An undo of a preset recall restores the values and routes; the recalled samples and
  name stay** (the director, 2026-11-02, carry-over row 402; §23 #86). An undo replays through
  the gesture path, so hosts record it as automation, or through the route path.
  Host automation and state loads push nothing, and nothing undoes while a gesture is open.
- **Themes** (D8, D9): dark by default, light by the header's switch, kept per machine in
  `settings.xml` (§18); every open editor follows a change within a second. The lanes stay dark
  in both themes. **The layer palette** (D4): Okabe–Ito's **A orange `#E69F00`, B sky blue
  `#56B4E9`, C bluish green `#009E73`, D reddish purple `#CC79A7`**, the letter beside the color
  everywhere it shows. The source families take four more colors: macros yellow (an ochre in
  light), modulators vermilion, MIDI blue, the follower gray, the transport the text color.
- **The keyboard:** Tab and Shift+Tab reach every control in reading order, the canvas, the pad
  and the header's buttons included; arrows step a focused control by 1/100 of its range (Shift
  1/1000), and on the canvas move the selected lane's window (Position and Pitch, Shift the
  spreads). The command modifier (⌘ on macOS, Ctrl elsewhere) with Z undoes, with Shift+Z or Y
  redoes, with 1 to 4 picks the view, with Shift+A to D the layer, and with I opens the About
  box; Escape ends a canvas drag or closes the box (the pad's drag ends with the mouse button).
- **The About box:** the product, its version (`gg::kVersion`), the format the host loaded, the
  license line, the repository's link, a line saying each release attaches its complete
  corresponding source, and
  the installers' license set, byte for byte (embedded at build time from the one list in
  `cmake/licenses.cmake`; a test compares each text with its file), as Markdown or plain text.
  It is modal. Carry-over row 221 is closed.
- **Audio → UI, by addition:** `UiSnapshot` gains a `LaneSnapshot` per layer (its write head,
  freeze, on state, built kind, base and effective Position, Spray, Pitch and Pitch Spread, and
  Feedback) and the base and effective Morph X and Y; M1's fields are unchanged.

**PR 2, as built** (2026-11-01; the spec's §6.3, §6.5, §6.7 and §16, VW-P2a to VW-P2n; §23 #71).
- **Panels:** Edit's **Ring** panel after Source (Ring Length, Loop, Loop Length, Overdub,
  Splice); Smear before Quality in Grain; the **Filter** panel with Bus Split and each bus on its
  own line (Filter, Cutoff, Resonance), no longer hidden. The FX view holds **Delay, Reverb,
  Limiter and Output** in the signal chain's order, in one wrapping panel grid shared with Edit.
  Every parameter is still placed once.
- **The lane menu:** a right-click on a lane, or Shift+F10 on the keyboard lane: Add splice mark
  here, Clear splice marks, Loop, Capture to layer A to D, Save capture as..., and on a Sample
  lane Load sample.... Marks and captures are commands, no undo entries; Loop is a gesture. A
  **Live lane spans its ring** (the ring's frames over the rate, 16 s when there is none), draws
  the loop window and numbered marks over its peaks and outlines the selected splice; its scan
  rectangle reaches Position up to the span. A file dropped on any lane loads into its layer, and
  on a Live layer says to set Source to Sample.
- **The sample strip** follows the selected layer in both products, in Perform and (compact) in
  Edit: name and status, Load, Relink, Sample folder, **Collect**, the **memory readout** ("N MB
  of M GB", in the notice color while over the cap) and the **cap menu**. Below 900 px the last
  two buttons fold into a "..." menu.
- **Undo:** a whole-state entry replays through `sendValues` in batches the UI queue takes, and
  arrow-key nudges on the same slots within a second merge into one entry.

### 18. State, presets and identities

**Session state** is a versioned, chunked container (since M1 sub-project 3; §23 #48), in
`gg/core/SessionState.h`:
- **The layout,** every integer little-endian: the magic `GGST`, a u32 container version (1),
  then chunks, each a four-character ID, a u64 payload length and the payload.
- `PTCH`: the patch, using sst-plugininfra PatchBase with migrations. Its bytes are the patch
  XML's, unchanged.
- `MEDI`: the media manifest, as XML. Version 1 (M1 and M2 sub-project 1) is a
  `<media version="1">` root with zero or one `<sample>` (M1 loads at most one sample). A
  sample's attributes:
  - `sha256`, 64 lowercase hex digits, and `size`, in bytes: the file's fingerprint;
  - `path`, absolute, and `relative`, to the sample folder with `/` separators (empty when the
    file is outside it), both UTF-8;
  - `format` (`wav`, `aiff`, `flac` or `mp3`), `channels` (as stored in the file), `rate` (the
    double's bits, as 16 hex digits, so it comes back bit for bit) and `frames`: the file's;
  - `embed`, always 0 in version 1.
- **`MEDI` version 2** (M2 sub-project 2, 2026-10-30; §23 #68), which this build writes: at most
  one `<sample>` and at most one `<ring>` per layer, the samples first, each in layer order. A
  version-1 entry loads as layer A's.
  - `<sample>` takes `layer` (`A`–`D`) first, then version 1's attributes in their order, then
    `content`, the content-relative reference (the file's path inside a content root, `/`
    separators, empty when it wasn't loaded from one; sub-project 5 names the roots), and
    `audio`, its `AUDI` payload's id when `embed="1"` ("0" otherwise). An embedded sample is a
    capture that never touched disk, with no `path` or `relative`; a filed capture's `path` is in
    the captures folder.
  - `<ring layer frames rate splices sha256 size embed audio | path>`: a saved ring, its window
    as float32 WAV bytes (oldest frame first), `rate` the host rate it was written at (as a
    double's bits), `splices` its marks as frames from the window's start (decimal, one space
    apart, at most 32), `sha256` and `size` of the WAV bytes, embedded (`audio`, no `path`) or
    filed (`path`, no `audio`). A ring is stereo, so it has no `channels`, and it has no `head`
    or `loop`: Freeze, Loop, Loop Length, Overdub and Splice are the layer's parameters in
    `PTCH` (ruling SP-T3d).
  - The attributes' escaping, the 64-deep pre-scan, the NUL rule and the all-or-nothing reading
    are version 1's; a malformed `<ring>`, or one with more than 32 marks, fails the state as a
    malformed entry does.
- `AUDI` (optional, since M2 sub-project 2): the embedded audio, one chunk holding every payload,
  so the container keeps one of each known ID: a u32 count, then per payload a u32 id (above 0,
  unique), a u64 length and the bytes, each a complete float32 WAV file (RIFF,
  `WAVE_FORMAT_IEEE_FLOAT`, the extensible form for stereo; a 44-byte header mono, 68 stereo;
  ruling SP-T3c), the bytes a filed capture has. An entry's `sha256` and `size` are of those
  bytes, so an embedded payload and its filed twin have one fingerprint. Written only when there
  is a payload. **Not FLAC** (the codec call, §23 #68): float32 restores bit for bit, so a
  restored ring's frames are the saved ones and every restore renders identically (the restore
  criterion, §23 #78), rather than within tolerance; a ring's floats, which may
  exceed ±1 (it clamps at ±2^20), need no scaling; one representation serves embedded and filed
  audio, `gg-media` decodes both (`decodeAudioBytes` over dr_wav's memory reader) and any tool
  opens a capture; and there is no encoder to pin. The cost: about 2.5× 24-bit FLAC's size, so
  the 32 MiB embed cap holds about 87 s of stereo at 48 kHz.
- `INST` (optional, since M2 sub-project 2): the per-instance settings,
  `<instance version="1" memoryCapMiB="1024" />`; absent, the defaults; a newer `version` loads
  its known attributes and keeps nothing else (it references no media). Since M2 sub-project 6's
  PR 2 the editor shows the cap and sets it (a menu in Perform's sample strip, 64 MiB to 16 GiB):
  a setting, not a parameter and no undo entry; changing it marks the state dirty, and a cap
  lowered under what is in use evicts nothing and only refuses the next load.
- **Reading is all or nothing.** A bad magic, another container version, a chunk running past
  the end, a missing or repeated `PTCH` or `MEDI`, or one that fails to parse rejects the whole
  state and changes nothing.
  - XML nested more than 64 elements deep, in `PTCH`, `MEDI` or the settings file, is rejected
    before it is parsed: tinyxml parses recursively, and a crafted project would overflow the
    stack (§23 #49). The depth is counted as tinyxml reads the document, and XML the count
    can't read the way tinyxml does is rejected too (§23 #53).
  - A `MEDI` of a newer version than the build reads loads with no sample (§23 #53). It must
    still be well-formed, with its `<media>` root alone and an integer `version`; nothing
    inside it is read. A future `MEDI` (any version) must pass the nesting pre-scan: at most 64
    deep, no `<?` after the first node, quoted attributes and well-formed `&#…;` references, or
    this build fails the whole state. The patch loads, and the instrument is silent, its status
    reading *sample from a newer version*. The manifest's bytes are kept, and a save writes them
    back unchanged, so an older build that saves over a newer session keeps its sample
    reference. A load or a relink replaces it, as it replaces any manifest. A newer `PTCH` migrates.
    Beside a newer `MEDI` this build keeps the `AUDI` chunk's bytes verbatim, unparsed, and
    writes them back too, so a newer build's embedded audio survives this build's save (ruling
    SP-T3b; the cost, a 64 MiB state copied once more).
  - **An entry naming a missing `AUDI` payload doesn't fail the state** (ruling SP-T3a): an M1
    or sub-project 1 build that re-saves a version-2 state keeps `MEDI` byte for byte and drops
    `AUDI`, an unknown chunk, and an older build's save should lose no more than it must (§23
    #53). The patch and every other entry load; a sample entry loads with its layer's status
    *embedded audio missing* (a session saved then leaves it out), and a ring entry is left out
    with that status. The cost: a corrupt state loads with a status instead of refusing.
  - An older build loads a state of this one with its patch and *sample from a newer version*,
    skipping `AUDI` and `INST`; this build loads M1's and sub-project 1's states unchanged.
  - Unknown chunk IDs are skipped, so a newer build's chunks don't break an older one.
  - A state that starts with `<` is a pre-container state, saved by sub-projects 1 and 2: all
    `PTCH`, with no sample.
  - The plugins refuse a state longer than 64 MiB, and read no further (`kMaxStateBytes`).
- **Writing.** Both products write the container: `PTCH`, `MEDI`, `AUDI` when there is a
  payload, and `INST`. Since M2 sub-project 2 both write their layers' entries in `MEDI` (the FX
  had none before; with no sample it still writes an empty one). `encodeState` never writes a
  `MEDI` entry that wouldn't decode: debug builds assert, and release builds leave it out, losing
  one sample reference rather than the whole session (ruling R9). A newer build's `MEDI` that
  this build loaded is written back byte for byte.
- **`MEDI` records the requested sample, not the one playing.** Only a load or a relink replaces
  it (and a restore sets it), so a session saved while its sample is missing, loading or failed
  to load still names it. Since M2 sub-project 2 each layer's loader records its own entry.

**What a session saves** (M2 sub-project 2, 2026-10-30; §23 #68):
- **A ring is saved when it is held** (the director's D1, option 1): its layer's Freeze or Loop
  base value on. A plain live ring refills from the input and isn't saved, so a project
  reopened with an unfrozen Live layer starts with an empty ring. Only a layer whose base Source
  builds to Live (Live or Resample) saves its ring (2026-10-31, the final review, the
  controller's ruling, ratified by the director on 2026-11-02, K-5, §23 #82: a Sample layer's ring
  records for a later switch, but no grain reads it; `gg::core::ringSaved`, the plugin's and
  `gg-render`'s one rule), so a Sample layer switched back to Live after a reopen
  starts with an empty ring too. A Spectral layer with no sample, whose analysis smears its ring,
  saves it as a Live layer does, and it is restored and armed as a Live layer's is; one with a
  sample saves none (2026-11-01, M2 sub-project 4's integration review, I2; §23 #69).
- **A saved ring is the loop window** (ruling SP-T8a): `min(L, N)` frames when Loop is on, else
  `min(head, N)`, from the Loop and Loop Length base values (ruling SP-T8b), with every mark
  inside it. The Splice selection doesn't narrow a save: Splice is a saved parameter and
  reselects on restore, and narrowing would lose the other splices. The cost: a save of a
  spliced ring keeps more audio than the selection, never less.
- **A save of a running ring keeps W frames** (ruling SP-T7c): it reads with the ring read
  (§14), retrying rather than waiting for a gap between `process()` calls, which the main thread
  can't see; a front the head lapped on the try kept is saved as silence, so the loop's period
  and the marks' offsets survive. A frozen ring's WAV bytes and fingerprint are cached by its
  storage epoch, head and window start (frozen rings only: a live ring's every write moves its
  head; ruling SP-T7d), so a host that saves every few seconds hashes an unchanged frozen ring
  once.
- **Loaded samples** are saved by reference, with their content path when they have one;
  **captures** loaded into a layer by the embed rule below.

**Sample handling**
- **What gets embedded:**
  - User samples are referenced, not embedded.
  - Captures and freezes are embedded up to **32 MB per instance**. Beyond that, they are written
    to a captures folder as files named by hash, and referenced from the state.
  - **As built** (M2 sub-project 2; §23 #68): at save, the rings and captures to persist are
    taken in layer order, rings before captures within a layer; each is embedded in `AUDI`
    while the payloads with it stay at or under `kEmbedCapBytes` = 32 × 2^20 bytes, and
    otherwise written to the captures folder as `<sha256>.wav`, float32 WAV (skipped when a file
    of that name and size is there), and referenced by path. So a session's embedded audio never
    exceeds 32 MB, and the state stays under the 64 MiB read cap. A write that fails leaves that
    entry out with the status *not saved: captures folder unwritable*, and the rest saves (R9's
    spirit).
  - **The captures folder** is a per-machine setting, `capturesFolder` in `settings.xml`;
    absent, `~/Music/Giga Grains/Captures` (Windows `%USERPROFILE%\Music\Giga Grains\Captures`,
    Linux `$XDG_MUSIC_DIR` or `~/Music`; the director's D7, option 1), made on first use.
- **Restore of the rings** (M2 sub-project 2, inside `stateLoad`, synchronous as D8 is): each
  saved ring, from its payload or its file found by the search below (its fingerprint
  verified; an embedded payload isn't hashed again, since the container already reads all or
  nothing, ruling SP-T6a), is decoded into a `RingStorage` of the layer's Ring Length, converted
  with r8brain if the host rate differs (the marks scaled by the WAV header's rate, ruling
  SP-T6b; a ring longer than the new length keeps its newest frames), checked against the
  memory cap, and offered with the layer's values and sample, so the first `process()` after
  opening has every ring and sample in place. Inactive, the rings wait for `activate()`. A ring
  that can't be found, whose payload is missing, that the cap refuses or that won't decode is
  left out with its status (*ring not found*, *embedded audio missing*, *ring not restored:
  …*). A restore's statuses stay until the next restore or until the layer is no longer held;
  an activation clears only a save's *not saved* messages (ruling SP-T7g).
  - **Deterministic, not continuous** (ruling SP-T7e; the restore criterion, as the director
    amended #64's on 2026-11-02, K-1, §23 #78): every restore renders identically and restores
    the saved frames bit for bit, but not identically to the saved session continuing, since the
    clock and the grain index restart at 0, as any reopened session's do.
- **Loading** (D8). A session restore loads its sample before it returns, so every render after
  opening is complete, and two exports of one project are identical.
  - `stateLoad` finds, verifies and decodes the sample on the main thread, where hosts expect a
    state load to take time. Active, it converts it and hands it to the audio thread together
    with the loaded values, so one `process()` call applies both. The exception: while two
    sources are still in play (a swap still draining), the engine can't adopt a third, so that
    call applies the values and the source follows within Size. Inactive, `activate()` converts
    it before it returns.
  - **A restore of the sample already in memory keeps it** (ruling R19, §23 #49): when the
    session's entry has the current entry's fingerprint (SHA-256 and size) and its sample is in
    memory, `stateLoad` reads no file. The sample and the current entry stay, and only the
    session's values go to the audio thread, which keeps its source. Undo, A/B compare and
    preset recall then take no time, and a file deleted since keeps playing.
  - A large sample makes opening a project a few seconds slower.
  - Loads by button, drop or relink decode in the background (§14).
  - Since M2 sub-project 2 (§23 #68) each layer has its own loader, in both products, sharing
    the instance's memory cap, the search cache and the sample pool; a restore runs the loaders
    in layer order, an embedded sample from its `AUDI` payload, then the rings. The
    instrument's layer-A strip drives loader A as before.
- **Finding a missing sample** (D5). Inside `stateLoad`, each candidate must match the
  manifest's size, then its SHA-256; the first that does loads. The candidates, in order (as
  M2 sub-project 2 extended them; §23 #68):
  1. the absolute path;
  2. each content root plus the entry's content-relative reference, when it has one (the roots
     and their order are sub-project 5's `gg/media/ContentPaths.h`; `findSample` takes them as
     an argument, and `gg-render` passes `--content-dir`);
  3. the sample folder plus the relative path (a moved drive, or another machine with the same
     library);
  4. the captures folder plus the file's name, for an entry whose saved path's folder is a
     captures folder (named `Captures`, the default's name on any machine, or the setting's
     folder's name), so a project moved with its captures folder reopens whole; a filed ring is
     found this way too;
  5. a file of the same name: in the saved path's folder, then in the last folder loaded from,
     then anywhere under the sample folder. An entry with no saved path (a factory preset's
     sample) takes the last component of its content-relative reference as the name, and has no
     saved folder to try first. That scan visits at most 50,000 entries, never follows a
     symlinked folder, and skips a folder it can't list.

  `SearchCache` remembers misses keyed also by the content roots and the captures folder.

  A match away from the saved path updates the manifest and marks the state dirty. With no
  match, the instrument stays silent and the status reads *missing*, or *changed* when the file
  at the saved path has other content. `gg-render` never searches: a render spec names its file.
- **Per-machine settings:** `settings.xml`, in `~/Library/Application Support/Giga Grains/` on
  macOS, `%APPDATA%\Giga Grains\` on Windows, and `$XDG_CONFIG_HOME/giga-grains/` (or
  `~/.config/giga-grains/`) on Linux. It holds the sample folder, which the user sets once, and
  the last folder loaded from, updated on each load, and since M2 sub-project 2 the captures
  folder (`capturesFolder`, above). It is read and written on the main thread only, through a
  temporary file unique to each save, renamed over it (ruling R16). Since M2
  sub-project 6 it also holds the editor's theme (§17; §23 #71): a
  `theme` attribute, `dark` or `light`, written only once a theme is chosen and read as dark when
  missing or anything but `light`, so the file's version stays 1. Both products read and write
  it, the FX for the theme alone; `GG_SETTINGS_FILE` points both at a scratch file in the tests.
- **Missing media:** shows placeholders, with relink and a "collect and save" command.
  - In M1 the instrument's loader strip shows *missing* or *changed* with Relink. A file whose
    fingerprint matches restores the sample; another loads as a replacement, and the status
    says so.
  - **Collect and save** (built in M2 sub-project 2; §23 #68; sub-project 6 wires the button):
    `gg::media::collectMedia` copies every referenced file that can be found (samples, filed
    rings and filed captures) into a folder, named `<name>` or, on a clash with other content,
    `<name>-<sha256 prefix>`, checks each copy's hash (a copy that fails is removed), and
    returns the state with each entry's `path` in the folder and `relative` recomputed, and a
    list of what it couldn't find. The plugin's `collectAndSave(folder)` saves first, then
    collects every file that save references, so an active instance's filed rings travel with
    the project (ruling SP-T7f), and marks the state dirty. Embedded audio needs no collecting.

**Presets.** There is one preset system: CLAP preset discovery. clap-wrapper bridges it to VST3
program lists and AU factory presets. As built (M2 sub-project 5, group 1; §23 #70):
- **The file.** A `.ggpreset` is a `GGST` container like a session, with a `<preset product
  author description tags>` element inside `PTCH` and no `AUDI`; `MEDI` is empty until a preset
  names a sample, which it does since group 2 (a content-relative reference: below). Older readers
  ignore the element. A preset is for one product: loading an FX preset into the instrument, or
  the reverse, fails with "not a Giga Grains FX preset" (D9). Tags come from a fixed vocabulary of
  lowercase words (`core/include/gg/core/PresetInfo.h`), which the packer enforces.
- **What a load keeps.** The instance's seed (D6), and, when the preset's `MEDI` names no sample,
  the sample already loaded (D5), so a settings-only preset plays on the user's own material. The
  live rings are not state. Every load goes through the one path a state load uses, so the audio
  thread sees what a restore shows it. **Since group 2 a load also keeps each layer's five ring
  parameters** (Ring Length, Loop, Loop Length, Overdub, Splice), which are the instance's capture
  and memory, so no preset load resizes a ring; a preset file may still hold them (an older one
  does) and they are ignored on load, and a factory source may set none (the packer refuses
  one). `gg-render --preset` does the same.
- **INIT** (D8) is the product's defaults and factory routes, built in memory, so it exists with
  no content installed. It is a fresh instance but for each layer's Ring Length, which it keeps so
  that no preset load resizes a ring (**D8a**, memory, not sound: the controller's ruling,
  ratified by the director, K-3, §23 #80; amended 2026-11-02: Loop, Loop Length, Overdub and Splice
  return to their defaults, as every INIT value does; a preset file keeps all five). It is one
  discovered preset, load key `init`, at a plugin location carrying both products' ids.
- **Discovery.** The factory (`CLAP_PRESET_DISCOVERY_FACTORY_ID` and its `_COMPAT`) is in
  `gg-plugin-core`, with one provider. It declares the `ggpreset` filetype; each content root's
  `Presets` folder that exists, flagged factory content; the user presets folder, always (created
  at `init`); and one soundpack, `org.sethbc.giga-grains.factory`. A FILE location is a native OS
  path on every platform.
- **The content roots,** in order, where a content-relative reference resolves (the first
  root that holds the file): `GG_CONTENT_DIR`; the settings' `contentFolder` (new, empty by
  default, set from the browser); the per-user install (macOS `~/Library/Application
  Support/sethbc/Giga Grains/`, Windows `%LOCALAPPDATA%\sethbc\Giga Grains\`, Linux
  `$XDG_DATA_HOME/sethbc/Giga Grains/`); and the system install (macOS `/Library/Application
  Support/sethbc/Giga Grains/`, Windows `%ProgramData%\sethbc\Giga Grains\`, Linux each of
  `$XDG_DATA_DIRS` plus `sethbc/Giga Grains/`). **A content reference never leaves its root:**
  one that is empty, has a backslash or a NUL, is not valid UTF-8, is absolute or has a `..`
  segment is refused (`isContentRelativePath`, the one definition, shared by the plugins' search
  and cache, `gg-render` and the packer). A relink by the user to another file drops the
  reference (the user chose another file).
- **The factory presets** (group 2; §23 #70): **24 per product, 48 in all**, in each product's
  `Presets` folder as `NN Name.ggpreset` (the order rule below), voiced with the mod matrix and
  tagged. The FX's 24 include 8 MIDI-driven ones (tag `midi`); the instrument's 24 read the
  content's samples by content-relative reference (`MEDI` version 2), all 51 files used, none
  more than twice. Their sources are `content/presets/<Product>/NN Name.json`, which
  `gg-presets` packs with `gg-render --pack-preset`. The tag vocabulary is 28 words: `brass`
  joined the character tags after `winds` (content v2 brought brass; `winds` misdescribed it),
  and `live-input` was struck (the director, the M2 gate, item 29; §23 #92).
- **The user presets folder** is beside the settings file (`Presets/`; on Linux
  `$XDG_DATA_HOME/giga-grains/presets/` for the default settings file). **Favorites** are per
  machine, `favorites.txt` beside the settings file, one preset key per line.

**Frozen from the first public release** (0.5.0's first beta, M5's; §23 #61, #62). Changing any
of these breaks users' saved sessions:
- CLAP parameter IDs (new ones are append-only);
- the AUv2 parameter order that follows from them (ascending parameter ID; §23 #60);
- plugin identities and AU codes.

**Frozen from 1.0.0:** factory preset order, which may move through the 0.5.x releases. The
order is the factory files' names, so the two-digit `NN` prefix of each (`NN Name.ggpreset`) is
part of it.

**Identities**

| Product | CLAP / VST3 | AU (type/subtype/manufacturer) |
|---|---|---|
| Giga Grains | Instrument; CLAP ID `org.sethbc.giga-grains` | `aumu`/`GGin`/`Sbcx` |
| Giga Grains FX | Audio effect with optional note input; CLAP ID `org.sethbc.giga-grains-fx` | **Both** `aufx`/`GGfx`/`Sbcx` (normal audio inserts) and `aumf`/`GGfx`/`Sbcx` (MIDI). Logic offers an `aumf` only in instrument slots, with audio arriving via side-chain. |

Both plugins share the vendor `sethbc`, the URL `https://github.com/sethbc/giga-grains` and the
AU manufacturer code `Sbcx` (display name `sethbc`). The FX's `aumf` entry is an extra identity
registered through clap-wrapper's legacy-identity factory. The values live in
`plugin/include/gg/plugin/Identity.h`.

**Derived identities.** These follow from the values above. Hosts and macOS store them too, so
they are frozen along with them.
- **VST3 class UIDs.** clap-wrapper derives each one from the plugin's CLAP ID as a name-based
  UUID: version 5, SHA-1 in the DNS namespace. Changing a CLAP ID therefore also changes its VST3
  UID.

  | Product | VST3 class UID (FUID string) |
  |---|---|
  | Giga Grains | `56E869BFEDD75019BD0C3105FA80B835` |
  | Giga Grains FX | `DBAF5C12EA095FEFB3A5CBBBB254442A` |

  These were read from the built VST3's factory and match Ableton Live's scanner log. pluginval
  prints only JUCE's hashes of them (`…-44681f8a` and `…-fc6462ac`).
- **Standalone app bundle IDs (macOS):** `org.sethbc.giga-grains.standalone` and
  `org.sethbc.giga-grains-fx.standalone`.
  - macOS keys each app's preferences and privacy permissions, such as microphone access, by its
    bundle ID.
  - clap-wrapper would give both apps the first ID, so `clients/clap-first/CMakeLists.txt` sets
    the FX's (ruling T8-a).

**Parameter ID range** (ruling T8-d):
- **Our own CLAP parameter IDs stay below `0xB00000`.**
- **clap-wrapper's VST3 wrapper adds parameters from `0xB00000` up**, for VST3's
  MIDI-controller mapping.
  - There are 2,096 of them (`0xB00000` to `0xB0082F`), plus 16 "MIDI Channel" units, each with
    a program list.
  - One of our parameter IDs in that range would shift every mapping parameter after it.
- **Hosts can save those IDs**, in automation or MIDI mappings for example, so treat them as
  frozen too. They depend on clap-wrapper's layout. A clap-wrapper update that changes the
  layout breaks saved sessions, just as changing one of our own IDs would.
- **Once a plugin has a preset-discovery factory (M2), the VST3 wrapper adds one more
  parameter:** a preset selector for the VST3 program list, at `0xC00000` or the first free ID
  above it. Hosts can save that ID too. Keeping our IDs below `0xB00000` keeps them clear of it.
  - **As observed and pinned** (M2 sub-project 5; §23 #70; clap-wrapper `4e7afc3`, unchanged, then;
    the bump to `4585bc6`, §23 #90, leaves it as it was): the selector
    is at `0xC00000` with `kIsProgramChange`, and no other ID lies in its block. macOS tests
    `identity_vst3_preset_selector_inst` and `_fx` read it from the built VST3. The wrapper
    publishes the list from a background crawl, in `onIdle`, so a first instance may show it
    empty; the AU lists the factory presets and loads one (tested in `gg-au-state-tests`).

**Parameter IDs** (M1 sub-project 1; §23 #37):
- **The scheme is `id = (scope << 16) | index`.**
  - Scope 0 is global; scopes 1 to 4 are layers A to D. M1 had layer A alone; layers B to D
    joined in M2 sub-project 1 at scope 1's indices (below), with nothing renumbered.
  - Index 0 is reserved.
  - Every ID stays below `0xB00000` (ruling T8-d). IDs are append-only: never renumbered, never
    reused.
- **The first ten,** defined in `gg/core/ParamIds.h`, pinned by a test
  (`tests/core/patch_tests.cpp`) and frozen at the first public release:

  | ID | Name | Range | Default |
  |---|---|---|---|
  | `0x000001` | Mix | 0–100% | 50% |
  | `0x000002` | Level | −60 … +12 dB | 0 dB |
  | `0x010001` | Position | Live layer: 0–16 s behind the write head in M1, 0–300 s since M2 sub-project 2 (`kPositionMax`); Sample layer: 0–100% of the loaded file | FX: 0.25 s; instrument: 0% |
  | `0x010002` | Size | 5 ms – 2 s | 150 ms |
  | `0x010003` | Density | 0.25–400 grains/s | 16 |
  | `0x010004` | Distribution | 0–100% (regular ↔ random) | 50% |
  | `0x010005` | Pitch | −24 … +24 semitones | 0 |
  | `0x010006` | Spray | 0–2 s (in the instrument, bipolar around Position) | 50 ms |
  | `0x010007` | Stereo | 0–100% per-grain pan spread | 50% |
  | `0x010008` | Freeze | off/on | off |

  - Every one is `AUTOMATABLE` and `MODULATABLE`; Freeze is also `STEPPED`. Modulation adds a
    clamped offset to the base value and is never written back.
  - Both products expose the same list, so they share one state format. In the instrument, Mix
    is `HIDDEN` and has no effect, and its remote-controls slot is empty (ruling M1-w); so is Dry
    to Reverb (below). Feedback, hidden there in M1, is shown in both since M2 sub-project 1
    (§14).
  - **Three parameters' `ParamMetaData` differ between the products,** never in ID or range
    (since M2 sub-project 1; §23 #65): Position (since sub-project 3; §23 #48), in every layer,
    in its default and display, the fraction of the loaded file in the instrument (§14); Source,
    in every layer, in its default, Live in the FX and Sample in the instrument; and Macro 8,
    "Macro Mix" in the FX and "Macro Level" in the instrument, at 50% in both (since sub-project
    5; §23 #58). Macro 7 differed in M1 too ("Macro Stereo" at 50% in the instrument); it is
    "Macro Feedback", unipolar, at 0% in both now, and an M1 instrument state folds its value
    into Stereo (The patch, below). All are CLAP values under one ID, so the state format stays
    shared, and a state moved between products keeps its stored values. The instrument also
    hides Mix and Dry to Reverb, as above. Freeze holds a Live layer's ring in either product,
    and does nothing to a Sample layer's grains.
  - The CLAP value is the parameter's sst-basic-blocks `ParamMetaData` value, a log-domain value
    for the log ranges. Values are floats end to end.
- **Space's eight** (M1 sub-project 4; §23 #50), appended after Freeze in the same file, pinned
  by the same test and frozen with the ten. They take the next free IDs in each scope past
  those sub-project 5's spec had reserved, `0x000011`–`0x000018` (the macros), `0x010009`
  (Texture) and `0x01000A` (Feedback), which it then took (below). The slot order is the
  parameter list's: the ten, then these eight in the order below. **The AUv2 order is ascending
  parameter ID,** not the slot order (§23 #60): clap-wrapper's AUv2 lists the AudioUnitSDK's
  parameter map, sorted by ID, unless a plugin provides its own ordering, which these don't.

  | ID | Name | Range | Default |
  |---|---|---|---|
  | `0x01000B` | Diffusion | 0–100% | 0% |
  | `0x000003` | Reverb Amount | 0–100% | 0% |
  | `0x000004` | Reverb Decay | 0.25–60 s, log (log2 s, −2 to `kDecayInfinite` = log2 60), then ∞ up to `kDecayMax` | 2 s |
  | `0x000005` | Reverb Size | 0–100% | 50% |
  | `0x000006` | Reverb Damping | 0–100% | 30% |
  | `0x000007` | Reverb Pre-delay | 0–500 ms (linear seconds, shown in ms) | 0 ms |
  | `0x000008` | Reverb Modulation | 0–100% | 25% |
  | `0x000009` | Dry to Reverb | 0–100% (FX only) | 0% |

  - Every one is `AUTOMATABLE` and `MODULATABLE`.
  - **Decay's top 2% of travel is ∞:** from `kDecayInfinite` (log2 60) up to `kDecayMax`, the
    top 2% of Decay's whole travel from −2 to `kDecayMax`. The display shows "∞" from
    `kDecayInfinite` up, the same boundary the reverb uses (§14, Space). Typing "∞" sets
    `kDecayMax`, and a bare number is seconds.
  - **A fresh instance is dry** (S4): Diffusion, Amount and Dry to Reverb default to 0, and
    with them at 0 the output is bit-identical to before Space. The other five are voiced at the
    listening check.
  - **Dry to Reverb is the FX's:** the instrument, with no dry input, marks it `HIDDEN`, and it
    does nothing there.
- **Sub-project 5's ten** (M1 sub-project 5, the Perform UI; §23 #58), appended after Dry to
  Reverb in the same file, pinned by the same test and frozen with the rest: the IDs its spec
  reserved. The slot order is the ten, Space's eight, then these ten in the order below: 28 in
  M1, before M2 sub-project 1's (below). The AUv2 order is ascending parameter ID (above;
  §23 #60).

  | ID | Name (FX / instrument) | Range | Default |
  |---|---|---|---|
  | `0x010009` | Texture | 0–100% (0 is M0's Hann, 100% gated; §14) | 0% |
  | `0x01000A` | Feedback | 0–95% (`kFeedbackMax`; the FX's in M1, a Live layer's in both products since M2 sub-project 1) | 0% |
  | `0x000011` | Macro Size | 0–100%, bipolar | 50% |
  | `0x000012` | Macro Density | 0–100%, bipolar | 50% |
  | `0x000013` | Macro Pitch | 0–100%, bipolar | 50% |
  | `0x000014` | Macro Spray | 0–100%, bipolar | 50% |
  | `0x000015` | Macro Texture | 0–100%, unipolar | 0% |
  | `0x000016` | Macro Freeze | off/on | off |
  | `0x000017` | Macro Feedback (both products since M2 sub-project 1; M1's instrument: Macro Stereo, bipolar, 50%) | 0–100%, unipolar | 0% |
  | `0x000018` | Macro Mix / Macro Level | 0–100%, bipolar | 50% |

  - Every one is `AUTOMATABLE` and `MODULATABLE`; Macro Freeze is also `STEPPED`.
  - **The macros are global** (scope 0) and relative: each offsets its targets along §14's
    recipes, and at its default it offsets nothing, so with them at rest a preset renders as its
    base values define it.
  - **Feedback was the FX's in M1:** the instrument marked it `HIDDEN`, and it did nothing
    there. Since M2 sub-project 1 it is shown in both products and acts on Live layers (§14).
  - They joined the patch without a version bump, as Space's did: a state saved before them
    loads them at their defaults and renders as before (a test decodes a literal pre-Perform
    state).
- **M2 sub-project 1's 190** (Layers and structure; §23 #65), appended after Macro 8 in the same
  file, pinned by the same test and frozen with the rest. **Layer parameters keep scope 1's
  indices in scopes 2 to 4,** so `slotForId` is arithmetic there. The slot order is the 28 M1
  slots, their order kept, then layer A's 37 new parameters, then layers B, C and D in full (48
  each, `B_Position` to `D_Step16`), then the nine new globals: **218 in all** (`kParamCount`;
  the director's D3 option 3 and D5 option 2, 2026-10-29). One X-macro table of the layer
  parameters (`GG_LAYER_PARAMS` in `gg/core/ParamIds.h`) generates the four layers' slots and
  IDs, so no name is typed four times. The AUv2 order is ascending parameter ID (above).

  **The layer scope**, by index in each of scopes 1 to 4 (`01`–`0B` are M1's, Position to
  Diffusion, as above; layers B to D have them at the same indices):

  | Index | Name | Range | Default |
  |---|---|---|---|
  | `0C` | Source | stepped: Sample, Live, Resample, Spectral, Oscillator (§14) | FX Live; instrument Sample |
  | `0D` | Pitch Spread | 0–24 semitones (D4) | 0 |
  | `0E` | Size Spread | 0–2 octaves (D5) | 0 |
  | `0F` | LFO 1 Rate | 0.01–50 Hz, log | 1 Hz |
  | `10` | LFO 1 Shape | stepped: Sine, Triangle, Saw Up, Saw Down, Square | Sine |
  | `11` | LFO 2 Rate | as LFO 1 | 0.25 Hz |
  | `12` | LFO 2 Shape | as LFO 1 | Triangle |
  | `13` | Env 1 Attack | 1 ms – 10 s, log | 10 ms |
  | `14` | Env 1 Decay | 1 ms – 10 s, log | 200 ms |
  | `15` | Env 1 Sustain | 0–100% | 100% |
  | `16` | Env 1 Release | 1 ms – 10 s, log | 200 ms |
  | `17` | Env 2 Attack | as Env 1 | 500 ms |
  | `18` | Env 2 Decay | as Env 1 | 1 s |
  | `19` | Env 2 Sustain | as Env 1 | 50% |
  | `1A` | Env 2 Release | as Env 1 | 1 s |
  | `1B` | Random 1 Rate | 0.01–50 Hz, log | 2 Hz |
  | `1C` | Random 1 Smooth | 0–100% | 0% |
  | `1D` | Random 2 Rate | as Random 1 | 0.5 Hz |
  | `1E` | Random 2 Smooth | as Random 1 | 50% |
  | `1F` | Step Rate | 0.01–50 Hz, log | 4 Hz |
  | `20` | Step Length | stepped 1–16 | 16 |
  | `21`–`30` | Step 1 … Step 16 | 0–100% | 0% |

  **The globals** (scope 0):

  | ID | Name | Range | Default |
  |---|---|---|---|
  | `0x000019` | Morph X | 0–100% | 50% |
  | `0x00001A` | Morph Y | 0–100% | 50% |
  | `0x00001B` | Layer A On | off/on | on |
  | `0x00001C` | Layer B On | off/on | off |
  | `0x00001D` | Layer C On | off/on | off |
  | `0x00001E` | Layer D On | off/on | off |
  | `0x00001F` | Level B | −60 … +12 dB | 0 dB |
  | `0x000020` | Level C | as Level | 0 dB |
  | `0x000021` | Level D | as Level | 0 dB |

  - Every one is `AUTOMATABLE` and `MODULATABLE`; Source, the LFO shapes, Step Length and the
    Layer On switches are also `STEPPED`. Layer A's Level is M1's `Level` (`0x000002`).
  - **Names:** every layer parameter is displayed with its layer's letter, "A Size" to "D Size"
    (M1's eleven included), with the CLAP `module` "Layer A" to "Layer D" for hosts that show
    paths; globals keep their names. `gg-render` takes the displayed names, and the bare M1 names
    as layer A's, so every M1 spec still parses.
  - **Position and Spray's text follows the layer's kind** (seconds behind the head on a Live
    layer, a percentage of the file on a Sample layer), read from Source's base value, not its
    effective one: a host can't rescan text at the modulation rate, and what is shown is what the
    user set (ruling FR-6).
  - **The next free index** is `0x31` in every layer scope and `0x22` in scope 0
    (`kNextFreeLayerIndex`, `kNextFreeGlobalIndex`). Scope 0's `0x0A`–`0x10` were never used and
    stay unused.
  - **Reserved for M2's wave 2,** in scope 0 and in every layer scope: `0x40`–`0x4F` for
    sub-project 2, `0x50`–`0x5F` for 3 and `0x60`–`0x7F` for 4 (`kReservedIndexBegin`,
    `kReservedIndexEnd`). Wave 2 appends its rows inside its block: a layer parameter's index is
    read from the table's index column, never from its row's position, so an index no row has
    (`0x31`–`0x3F`, a block's unused ones) names no parameter, and scope 0's table spans
    `0x00`–`0x7F` alike (ruling FR-1). A test fails if an index passes `0x7F`, if the next-free
    constants differ from one past the table's largest index below the blocks, or if an index no
    row has names a parameter. Wave 3 adds no parameters.
- **M2 sub-project 3's five** (Engine scale and quality; §23 #66), in its reserved block and
  frozen with the rest: **Quality** in every layer scope at index `0x50` (`A Quality` to `D
  Quality`; stepped, 0 Eco, 1 Normal, 2 HQ; default 1; both products; a mod target), so layer
  A's parameters number 49, and **Budget** at `0x000050` in scope 0 (stepped, 256 to 16,384
  grains, default 4,096; §14), **223 in all**. The slot numbers of layers B to D and of the
  later globals shifted by one each; no ID changed. A pinned test expects `0x50` used in every
  scope and `0x51`–`0x5F` free. The patch stays at version 2: a state without them loads the
  defaults and plays as it did. Neither is on a remote-controls page (§17's thirteen).
- **M2 sub-project 2's twenty** (Sources and persistence; §23 #68): five per layer in its
  reserved block, `0x..40`–`0x..44` in each of scopes 1 to 4 (`GG_LAYER_PARAMS_M2_SOURCES`,
  appended to `GG_LAYER_PARAMS`; `0x45`–`0x4F` stay unused, and scope 0's block takes nothing,
  since the memory cap is a setting), displayed with the layer's letter, module "Layer A" to
  "Layer D": **243 parameters in all** with sub-project 3's five, 54 per layer. Pinned by the same test (`tests/support/
  ParamTable.h`); the next-free constants are unchanged. No patch version bump: a state without
  them loads them at their defaults.

  | Index | Name | Range | Default | Flags |
  |---|---|---|---|---|
  | `40` | Ring Length | 1 s – 300 s, log (log2 seconds) | 16 s | neither automatable nor modulatable by its flags, and refused as a route's target (ruling SP-T12c, §23 #68). The main thread resizes the ring (§14) |
  | `41` | Loop | off/on | off | stepped |
  | `42` | Loop Length | 50 ms – 300 s, log (log2 seconds); clamped to the ring in effect | 1 s | |
  | `43` | Overdub | 0–100% | 0% | ramped per sample |
  | `44` | Splice | stepped 0–32 (0: the whole window) | 0 | stepped |

  - All but Ring Length are `AUTOMATABLE` and `MODULATABLE`. Loop Length, Overdub and Splice
    are routes' targets like any layer parameter; Ring Length is refused as one (ruling SP-T12c,
    §23 #68).
  - **Position's range widens** to the longest ring without moving any M1 value: the Live
    mapping `(16/255)(2^(8v) − 1)` seconds stays, and the CLAP maximum rises from 1 to
    `kPositionMax` = 1.5279342 (`log2(300 × 255 / 16 + 1) / 8`, rounded to the nearest float;
    ruling SP-T2a), where the mapping gives 300 s. A Sample layer's Position is still the
    fraction of its file, clamped to 1, and typing "100%" there parses to 1.0, not the maximum,
    so a later switch to Live reads M1's 16 s rather than 300 s (ruling SP-T2b). A range change
    before any freeze: the ID is the same, and hosts rescan on the next load.
- **M2 sub-project 4's forty** (the Extremes; §23 #69), inside the `0x60`–`0x7F` blocks, each
  `AUTOMATABLE | MODULATABLE` and the stepped ones `STEPPED`, with no metadata that differs
  between the products: **283 in all** with sub-project 1's 218, sub-project 3's five and
  sub-project 2's twenty (`kParamCount`), 62 per layer. One list, `GG_LAYER_PARAMS_M2_EXTREMES`,
  gives each layer eight rows after sub-project 3's, so the index column stays ascending; the next-free
  constants are unchanged. **The (name, ID)s are what is pinned**, in `tests/support/ParamTable.h`,
  not a total. `0x68`–`0x7F` stay unused. The layer scope:

  | Index | Name | Range | Default |
  |---|---|---|---|
  | `60` | Smear | 0–100% | 0% |
  | `61` | Bus Split | 0–100% (the share of grains on bus 2) | 0% |
  | `62` | Bus 1 Filter | stepped: Off, Low-pass, High-pass, Band-pass, Notch | Off |
  | `63` | Bus 1 Cutoff | 20 Hz–20 kHz, log | 1 kHz |
  | `64` | Bus 1 Resonance | 0–100% | 0% |
  | `65`–`67` | Bus 2 Filter, Cutoff, Resonance | as bus 1's | Off, 1 kHz, 0% |

  Scope 0:

  | ID | Name | Range | Default |
  |---|---|---|---|
  | `0x000060` | Delay Amount | 0–100% | 0% |
  | `0x000061` | Delay Time | 1 ms–2 s, log | 375 ms |
  | `0x000062` | Delay Feedback | 0–100% | 40% |
  | `0x000063` | Delay Damping | 0–100% | 30% |
  | `0x000064` | Delay Ping-pong | stepped off/on | off |
  | `0x000065` | Limiter Mode | stepped: Off, Soft, Peak | Off |
  | `0x000066` | Limiter Ceiling | −24 … 0 dB | −1 dB |
  | `0x000067` | Limiter Release | 10 ms–1 s, log | 100 ms |

  The patch stays at version 2: a state that doesn't mention them loads them at their defaults,
  and a literal wave-1 state decodes in a test and renders M1's bytes. The container is
  unchanged, and the AUv2 order is ascending ID as ever.
- **The patch** (the state's `PTCH` chunk) is `gg-core`'s versioned XML: patch id
  `org.sethbc.giga-grains.patch`, version 2 since M2 sub-project 1 (1 through M1; below). Each
  value also carries its float bits, so a saved
  and reloaded state renders identically, and the engine seed is saved with it. **A new instance
  draws its seed from the OS** (since sub-project 5; §23 #59): two 32-bit draws from
  `std::random_device`, on the main thread at construction, or, if that throws, a splitmix64 of
  the steady clock. Two instances on two tracks make different clouds, and the state carries
  the seed, so a reopened session renders identically; `gg-render`, the tests and the goldens
  keep explicit seeds. **A state loaded while active hands its seed to the audio thread** in the
  plugin's loaded-state snapshot (`loadedState_`, in both products; the FX's values ride with
  it, the instrument's with its sample, so there the two may land in different blocks, ruling
  PU-T5a), and the engine keys every grain scheduled from that `process()` call on by it; no
  restart is asked for. Once the audio thread has applied
  a loaded state's values, the plugin asks the host, from the main thread, to rescan them
  (`CLAP_PARAM_RESCAN_VALUES`). Space's and sub-project 5's values joined the patch without a
  version bump: a parameter a state doesn't mention takes its default, so a state saved before
  them loads with them at rest and renders as before. Since M2 sub-project 1 the mod matrix's
  routes ride with the seed in that snapshot, so in the instrument they too may land a block
  apart from its values (ruling LS-T14a, as PU-T5a). **Since M2 sub-project 6 the editor sets
  routes too** (the Mod view and the rings; §23 #71), on the main
  thread, through `uiSetRoutes`: `routes_` is updated, so a save carries them, the host is told
  the state is dirty, and the routes reach the audio thread through a channel of their own, a
  `LatestValue<ModRoutes>` taken after the loaded state, since one channel would let an edit
  overwrite a pending load. A state load while active also pushes its routes onto that channel,
  so the later of a load and an edit wins in either order, and a deactivation drops an edit the
  audio thread never took (ruling VW-T4a). A route edit makes no parameter event.
  - **Version 2** (M2 sub-project 1, M2's one patch version bump; §23 #64, #65) adds the 190 new
    parameters' `<p>` entries and, after `<engine>`, a `<mod>` element holding the mod matrix's
    routes: one `<route>` per slot in use, in slot order, an empty slot left out.

    ```
    <mod><route slot="1" source="Macro 1" sourceLayer="same" target="65538" targetLayer="all"
      depth="3.000000000000" bits="40400000" curve="Bipolar" /> …</mod>
    ```

    `slot` is 1 to 48; `source` and `curve` are their stable identifiers (`modSourceName`,
    "Macro 1" to "Transport Playing", and `modCurveName`, "Unipolar" to "TopQuarter"); `target`
    is the target slot's CLAP ID in decimal, as `<p id>` writes it (layer A's for a per-layer
    kind), never its name, since IDs are frozen and names aren't (ruling LS-P1);
    `sourceLayer` is `A` to `D` or `same`, `targetLayer` `A` to `D` or `all`; `depth` and
    `bits` are written and read as `<p>`'s `v` and `bits` are. The writer leaves out a route
    that wouldn't read back as itself. A malformed `<route>` loads as an empty slot and the load
    goes on; of two naming one slot, the later wins; unknown attributes, and elements other than
    `<route>` inside `<mod>`, are ignored (a newer version's).
  - **The migration from version 1** (an M1 state): every new parameter takes its default, so
    layer A's Source is the product's kind (an M1 FX state is Live, an M1 instrument state
    Sample); any `<mod>` is ignored and slots 1 to 12 take the factory routes; and **the
    instrument's Macro 7 is folded:** Stereo's base becomes `clamp(stereo + 0.5f × (2v − 1), 0,
    1)`, M1's recipe expression, with `v` the saved Macro 7 (M1's default 0.5 when the state has
    none or it is NaN), both first clamped to 0–1 as M1 loaded them, and Macro 7, Macro Feedback
    now, is set to 0, its rest. The render after the migration is M1's, bit for bit (Feedback 0 on
    a Sample layer does nothing). The FX's Macro 7 was Macro Feedback already, and is untouched.
    A pre-container state migrates the same way.
  - **A version-2 state in the M1 build** loads through PatchBase's newer-version path: unknown
    `<p>` IDs are ignored and `<mod>` is unread, so layer A's values load and the M1 recipes
    apply. A test loads a version-3 state with an extra element into this build, since the M1
    binary isn't available to the tests.
  - The container stays at version 1; `AUDI` and `MEDI` are unchanged (sub-project 2 alone adds
    chunks; §23 #64).
- **Remote controls:** thirteen 8-slot pages since M2 sub-project 4 (eight from sub-project 1,
  three in M1), each page's
  `page_id` its index. "Perform" (since sub-project 5; §23 #58), first: the eight macros in
  order, Macro Size to Macro Mix (in the instrument, Macro Level in slot 8; M1's also had Macro
  Stereo in slot 7). "Cloud": layer A's Position, Size, Density, Pitch, Spray, Stereo, Freeze,
  Mix. "Space" (since sub-project 4): Diffusion, Reverb Amount, Decay, Size, Damping, Pre-delay,
  Modulation, Dry to Reverb. Then M2 sub-project 1's five (§23 #65): "Layers" (Layer A On to
  Layer D On, Level, Level B, Level C, Level D), "Morph" (Morph X, Morph Y, then six empty
  slots), and "Cloud B", "Cloud C" and "Cloud D" (that layer's Position, Size, Density, Pitch,
  Spray, Stereo, Freeze and Level). Then M2 sub-project 4's five (§23 #69): "FX" (Delay Amount,
  Time, Feedback, Damping and Ping-pong, then the limiter's Mode, Ceiling and Release) and
  "Filter A" to "Filter D" (that layer's Smear, Bus Split and its two buses' Filter, Cutoff and
  Resonance). In the instrument, the Mix and Dry to Reverb slots are empty.
  Inserting "Perform" moved "Cloud" and "Space" from page IDs 0 and 1 to 1 and 2, before any
  release, with their contents unchanged; sub-project 1's pages took IDs 3 to 7 and sub-project
  4's 8 to 12, the earlier pages keeping theirs.
- **The tail** (CLAP `tail`, since sub-project 4; ruling R-T7a, §23 #50) is a constant per
  product, and the plugins never call `tail.changed`:
  - **the FX's is infinite** (`CLAP_TAIL_INFINITE`): a live-buffer effect can sound with no
    input at any time (Freeze, a held note, a 16 s read, an infinite reverb);
  - **the instrument's is its longest finite one,** 122.5 s at the active rate: Size's 2 s
    release, plus Pre-delay's 0.5 s, plus twice Decay's longest finite 60 s.
  - A tail that followed the values would need `tail.changed`, which clap-wrapper turns into
    VST3's `restartComponent(kLatencyChanged)`: the host deactivates and reactivates the plugin,
    which would empty the FX's ring and drop its notes and Freeze at every note, Freeze toggle or
    Amount change. The cost: the FX never sleeps on silence in hosts that suspend silent
    plugins, and an instrument at ∞ is suspended 122.5 s after its last note.
  - clap-wrapper's AUv2 hard-codes `SupportsTail()` false, so AU hosts never see either tail
    (§15; carry-over). The plugins keep processing through the tail.

**CLAP features by 1.0.0** (M1 ships `params`, `note-ports`, `state`, `tail`, `audio-ports`,
`remote-controls` and `gui`; M2 sub-project 1 adds `thread-pool` and the instrument's sidechain
input, which every validator accepts, so D2's fallback was not needed; §14, §15):
- `params` with per-note modulation flags
- `note-ports` (CLAP, MIDI and MIDI-MPE dialects)
- `voice-info`
- `state` and `state-context`
- `preset-load`
- `tail` (a constant per product, above)
- `latency` (0)
- `audio-ports` (with an optional sidechain: the instrument's, since M2 sub-project 1)
- `render`
- `remote-controls` (thirteen pages, above)
- `param-indication`
- `track-info`
- `gui`
- `thread-pool` (optional; since M2 sub-project 1, §15)

### 19. Verification strategy

- **The engine is a headless library.** It builds and tests without any plugin framework.
- **Unit tests (Catch2):**
  - grain onset accuracy
  - window sums
  - interpolators (mipmaps from M2)
  - live-buffer head crossing
  - parameter ranges
  - state round-trip and migrations
- **`gg-render` offline CLI.** It takes a JSON render spec (a built-in or file input, parameter
  values, automation and notes; a preset from M2) and writes a WAV, metrics, a grain-event log
  and a spectrogram deterministically. It's the backbone of audio testing and how Claude
  "listens".
  - **Sessions and ring operations** (M2 sub-project 2; §23 #68): `state` names a `GGST` file
    restored before the render as a plugin's `stateLoad` would (values, routes, samples, rings
    and the memory cap; `params` then override), its samples found only through
    `--content-dir` and the spec's directory, never searched by name, and the spec's seed, not
    the state's, is the render's; `--save-state` writes the session at the render's end, the
    held rings and captures embedded or, past the embed cap, filed in `--captures-dir`, file
    samples by reference (a built-in signal sample has no file and isn't saved); `operations`
    are timed `splice`, `clearSplices` and `capture` steps, `gg-render` cutting its calls at a
    capture's frame so the capture lands on that sample at any block size (a capture's `file`
    is relative to the working directory, like `-o`); `memoryCapMiB` sets the cap, so a refusal
    is testable (exit 2). Ring Length is set in `params` before the render, never automated
    (ruling SP-T8c).
  - **Presets** (M2 sub-project 5, group 1; §23 #70): `--preset FILE` (or the spec's `"preset"` key)
    renders with a `.ggpreset` applied first, so the spec's `params`, `mod` and `samples` go on
    top and the spec's seed is the render's; a preset for the other product, or a preset beside a
    `state`, is bad input (exit 2). `--pack-preset <source.json> -o <out.ggpreset>` compiles a preset source in the spec's
    vocabulary, and a name the product doesn't display or a tag outside the vocabulary fails it.
    `--content-dir DIR` is the folder content-relative samples resolve against (since group 2 a
    preset's samples are content-relative; an entry with only an absolute path is refused,
    because `gg-render` never searches).
  - **`--mutate-preset <in.ggpreset> -o <out.ggpreset> --amount A --counter N`** (group 2; the
    listening pack's Mutate ladder) writes the preset it reads with its values moved by one
    Mutate press: the plugin's `gg::core::mutate` with seed 0, the default locks, the amount (0
    to 1) and the press counter, name, tags, routes and samples unchanged. It takes no other
    option; amount 0 gives the input back; a bad amount or counter, or a file that isn't a
    preset, exits 2.
  - **Tests of the preset system:** a bit-identity test (`std::memcmp`) of the headless FX
    after `preset-load` of a fixture preset against `gg-render --preset` of the same spec, in
    both orders of loading and activating; the `content_fixture` test, which fetches the pinned
    content into the build tree for the cases that read it (offline once fetched); the
    discovery provider through the entry point, and the catalog; the VST3 selector and the AU
    factory presets (§18). The 48 preset goldens (one per factory preset) came with group 2
    (below); the existing goldens' WAV hashes stayed unchanged through group 1 (36 before main's
    merge, 43 after; §23 #70).
  - **The 48 preset goldens** (group 2; §23 #70; the spec §10), beside the 60 cases, so **108
    goldens**: one per factory preset, in `tests/golden/presets/cases/` (expected files in
    `tests/golden/presets/expected/`), 6 s at block 256, seed 1, each rendering its packed preset
    from the fetched content (`--content-dir`). The FX's 24 run over the generated sweep (22) or
    `loop-48k.wav` (18, 21), the eight MIDI ones with notes; the instrument's 24 play a two-note
    phrase and a chord in each preset's register. They compare as every golden does (metrics
    within tolerances, the grain-event log exactly per line, across platforms). They stay out of
    the click-injection table: in its place a test requires each expected click score under 15,
    a peak under -1 dBFS and nothing non-finite or silent (the scores run 0.00 to 8.80), and
    that the cases and the sources match one to one. A preset source's change moves its golden;
    the 60 earlier cases' WAV SHA-256s were unchanged through every task of group 2
    (`scripts/update-goldens.sh` before and after). The listening pack (`docs/listening/m2-presets/`)
    renders the FX presets over a rendered strings phrase, not one held note.
- **Golden renders compared by metrics:**
  - spectral distance
  - RMS envelope
  - peak and DC
  - stereo correlation
  - click detection (the click score, `metricsVersion` 2: above the limit of 15, normalized by
    the level within ±5 ms; it has a blind spot, the two dense-noise cases, whose own noise hides
    every injected -20 dBFS step and every 0 dBFS spike (scores 9.3 to 14.6), accepted by the
    director 2026-10-29, §23 #57; and `fx-feedback`, which catches 7 of the 16 (below), its
    table entry also accepted by the director 2026-10-29, §23 #58)
  - NaN, Inf and denormal checks

  Sub-project 5 added three cases (§23 #58): `fx-macros` (each macro stepped across its travel
  over a 330 Hz sine, Macro Freeze last), `fx-feedback` (a 4 s sweep, Feedback 0.5 then 0.9,
  Freeze from 5 s to 6.5 s) and `inst-texture` (a held note, Texture stepped from 0 to 1).
  `fx-macros` and `inst-texture` catch all sixteen injected steps; `fx-feedback` catches 7, the
  rest hidden from about 2.8 s to 6.6 s by the sweep's top octaves, their regeneration at
  Feedback 0.9 and the frozen ring, which the click-injection table pins.

  M2 sub-project 1 added fourteen cases (§23 #65), each 6 to 8 s, so there are 36:
  - the layers: `fx-layers` (stereo noise, four Live layers, B up a fifth, C's Size 40 ms, D an
    octave down, the pad at rest), `inst-layers-morph` (four Sample layers over a sweep, a
    330 Hz sine, noise and a lower sweep, one held note, Morph X and Y stepped around the square
    and back to the center), `inst-pitch-spread` (one note over a sine, Pitch Spread 0, 2, 7,
    12 and 24 semitones at 1 s intervals), `inst-size-spread` (one note over noise, Size 100 ms,
    Size Spread 0, 0.5, 1 and 2 octaves), `fx-layer-switch` (layer B on at 1 s, off at 2.5 s, on
    at 3 s, over a 330 Hz sine: the ramps and the forced onset), `fx-source-switch` (layer A
    Live, then Sample over `loop-48k.wav` from 1 s, Live again from 2.5 s: the sounding grains
    finish on their source) and `fx-macros-layers` (Macro Size, then Macro Pitch, stepped with
    layers A and B on: the factory routes on every layer);
  - the modulation sources: `fx-mod-lfo` (LFO 1's sine at 0.5 Hz to Position, LFO 2's square at
    2 Hz to Pitch ±12, over a 50–700 Hz sweep), `inst-mod-env` (Env 1 to Size and Level, Env 2
    to Diffusion, two notes with a gap), `fx-mod-random` (Random 1 at 4 Hz to Pitch, Random 2 at
    1 Hz with Smooth 50% to Stereo), `fx-mod-step` (Step at 4 Hz, Length 12, to Pitch ±12),
    `inst-mod-midi` (the mod wheel to Size, aftertouch to Density, pitch bend to Pitch,
    velocity to Level and key to Texture, over three notes), `fx-mod-follower` (the follower of
    `loop-48k.wav`'s bursts to Density) and `fx-mod-transport` (120 BPM playing, the beat phase
    to Position and the bar phase to Pitch, over the same sweep). Transport Playing has no
    golden: `gg-render`'s transport is steady, so a golden could only show a constant offset,
    and the `[core][mod]` test covers it (ruling FR-7).

  Ten catch all sixteen injected steps (the two sweeps end at 700 Hz, so the octave the LFO and
  the bar phase add stays below the top octaves that hide steps). The click-injection table
  pins the other four with their reasons: `fx-layers` catches none, the four layers' noise bed
  hiding every step, as in the dense-noise cases; `fx-mod-follower` catches 15, the burst at
  3.5 s, re-grained densely 20 ms behind the head, hiding the step at 3.53 s;
  `inst-layers-morph` catches 5, layer C's noise sample hiding the steps except where its
  weight `(1 − x)y` is near 0, on the pad's Y = 0 and X = 1 edges (1.0 s to 3.0 s) and near the
  corner X 0, Y 0 (about 5.2 s); and `inst-size-spread` catches 3, the note's noise bed hiding
  the rest but for a gap between grains at 1.8 s and after the note's last grains, at about
  5.1 s. With layers B to D off, the pad at rest, both spreads 0 and the factory routes alone,
  every one of the 22 earlier cases' WAVs kept its SHA-256 through every task.

  M2 sub-project 3 added seven cases (§23 #66), 3 to 8 s each, so there were 43, and its
  follow-up the 44th, `inst-sample-end` (the instrument over a 2 s 660 Hz sine at Position 0.97,
  every grain reaching the sample's end inside its window and fading out with the raised cosine;
  carry-over row 300, §23 #66 as amended). The seven:
  `sparse-sine-hq` and `sweep-stretch-hq` (M0's `sparse-sine` and `sweep-stretch` at HQ, in
  sample mode), `fx-hq-pitch-ladder` (layer A at HQ reading a 20 kHz to 50 Hz sweep, Pitch
  stepped 0, +12, +24, then two notes for +36 and +47: levels 0 to 3, and 3 stretched; the 5 s FX
  input is never heard, since layer A reads its Sample at Mix 1), `fx-quality-switch` (Quality
  stepped Eco, Normal, HQ, Normal at 1 s intervals over a 330 Hz sine at Pitch +7: the per-grain
  latch), `fx-overload` (four layers over a 330 Hz sine, A and B at HQ, C Normal, D Eco, Budget
  4,096, 1,024, 256, 4,096, layer D off at 5 s and on at 5.5 s: the ladder and the quotas moving
  with the rendering set; its event log is 1.6 MB), `inst-overload` (two notes, one HQ layer of
  1 s grains at Density 400 Hz, Budget 256: sustained stealing) and `fx-hq-live-level0` (layer A
  Live at HQ and Pitch +12 over a 50 Hz to 20 kHz sweep: the ring has one level, so every line
  reads `"lvl":0`; regenerated as `fx-hq-live` by M2 sub-project 2's Task 12 when the ring's
  levels landed, every line then `"lvl":1`). Five catch all sixteen injected steps; the
  click-injection table pinned `fx-hq-live-level0` at 9 (the ring's sweep read an octave up
  hides the steps from about 2.1 s; `fx-hq-live` at 10, the fold-back gone) and `fx-hq-pitch-ladder` at 3 (every
  grain reads part of the sweep and the ladder carries content near Nyquist to every step). Every
  one of the 36 earlier cases' WAV SHA-256 was unchanged through every task, and their event
  logs were rewritten once, by inserting `"tier"` and `"lvl"` alone.

  M2 sub-project 2 added seven cases (§23 #68), so there were 51: `inst-resample-feedback` (layer
  A Sample over a 220 Hz sine, one held note, layer B Resample at Pitch +12 with Feedback 0.6:
  the instrument granulating its own output), `fx-resample` (layer A Live over a sweep, layer B
  Resample at Pitch −12, Position 0.5 s), `fx-long-ring` (24 s, Ring Length 60 s, an 8 s burst,
  then Position stepped to 10, 18 and 22 s behind: history past 16 s), `fx-loop-overdub` (Loop
  1 s, Overdub 0.7 over `loop-48k.wav`'s noise bursts, Freeze at 4 s, Overdub to 0 at 6 s),
  `fx-splice-capture` (a sweep, Freeze at 2 s, two splices, Splice 1 then 2, splice 2 captured
  into layer B at 4 s and played at Pitch +7, Size 50 ms, Density 32 Hz, so both Splice changes
  fall under sounding grains: the window fix's gate, ruling SP-T12aa), `fx-ring-restore` (the
  generated `frozen-ring.ggst`, a
  20 s ring frozen with two splices and embedded, restored with Freeze on and scanned for 4 s:
  the restore gate) and `inst-four-samples` (the generated `four-samples.ggst`, four Sample
  layers found by content-relative reference under `--content-dir`, one held note: `MEDI`
  version 2 and the content reference). Their state files are written by `gg-render
  --write-media`, never committed. With Ring Length 16 s, Loop off, Overdub 0, no splice, no
  Resample layer, nothing embedded and the cap at its default, every one of the 36 earlier
  cases' WAVs kept its SHA-256 through every task, and every one of main's 43 kept its
  SHA-256 through the merge of sub-project 3 into this branch; the ring pyramid's task (Task 12)
  moved one case alone, `fx-hq-live-level0`, regenerated as `fx-hq-live` with every line at
  `"lvl":1`; every other case's WAV kept its SHA-256. The merge of sub-project 3's follow-up
  (main at `1d62946`) kept every one of main's 44 cases' WAV SHA-256 but that one, its
  `fx-overload`, `inst-overload` and `inst-sample-end` among them.

  M2 sub-project 4 added nine cases (§23 #69), so there are 60: `inst-smear` (one note over noise
  at a static Position, Smear stepped 0, 0.25, 0.5, 1), `inst-stretch-smear` (`loop-48k.wav` at
  1000× through LFO 1 to Position, Smear 0 for 4 s, then 1), `inst-spectral` (layer A Spectral
  over a sweep sample, Position stepped 0.1, 0.4, 0.7), `fx-spectral-freeze` (layer A Spectral
  over the loop as the input ring, Freeze from 2 s to 4 s), `fx-bus-filters` (stereo noise, Bus
  Split 50%, a low-pass and a high-pass, a cutoff and a type automated), `inst-filter-lfo` (a
  note over noise, LFO 1 to Bus 1 Cutoff ±2 octaves), `fx-delay` (the loop, ping-pong),
  `fx-delay-automate` (Time's glide, Feedback to 90%, Amount to 0) and `fx-limiter` (four layers
  at +6 dB, Mode Off, Soft at 2 s, Peak at 4 s, Ceiling −6 dB). Seven hide some injected steps,
  pinned in the click-injection table with their reasons: `fx-limiter` and `inst-smear` catch
  none and `fx-bus-filters` 1 (a noise bed throughout), `inst-filter-lfo` 13 (the three crests
  where the cutoff is at 4 kHz), `fx-delay` 12 (the loop's bursts and their echoes),
  `inst-stretch-smear` 9 and `fx-spectral-freeze` 9 (the smooth wash hides the steps once the
  smear is on); `inst-spectral` and `fx-delay-automate` catch all sixteen. Peak did not click in
  `fx-limiter` (clean score 4.31). **The comb test** (`tests/metrics/comb_tests.cpp`) is the
  exit criterion: Smear 1's cepstral comb score is 16 to 17 dB below Smear 0's at a static
  Position and at 1000× (pinned from measurement: static 33.89 against 17.92 dB, 1000× 34.71 against
  17.52 dB, limits 28 and 22). Every one of the 36 earlier cases kept its WAV SHA-256 through
  every task, with Smear 0, Bus Split 0, the filters Off, the delay at Amount 0 and the limiter
  Off; after the merges, the 51 earlier cases kept theirs but `fx-layer-switch` and
  `fx-overload`, which row 301's morph fix moved inside their Layer On ramps (§23 #69).

  No golden pins the audio's bits (§23 #52). The bit-exact checks are:
  - every golden's grain-event log (each grain's layer, onset, position, rate, duration,
    texture, gain and pan: the grains only, not Level, the morph, Mix, Feedback's regenerated
    audio or Space after them), compared exactly on every platform. Texture joined the line as
    `"tex"` in sub-project 5 (§23 #58), and the layer as `"layer"`, after `"i"`, in M2
    sub-project 1 (§23 #65); each rewrote every `.events.jsonl` once (the second only by
    inserting `"layer":0`) and left every WAV's SHA-256 alone. In M2 sub-project 3 the line gained
    `"tier"` (0 Eco, 1 Normal, 2 HQ, after the ladder) and `"lvl"` (the latched mip level), after
    `"tex"` and in that order (a third rewrite, by insertion alone), and a stolen grain has a line
    of its own, `{"i":k,"layer":L,"steal":o}`, emitted right before the line of the grain it made
    room for, `o` that grain's onset sample, so the cross-platform gate sees every decision of the
    ladder (`formatEventLine`, `formatStealLine` in `tools/gg-render/Render.h`). **Across layers the log is in
    piece, layer, spawn order** (ruling LS-T10b): pieces end at call ends, at grid boundaries
    with changes, at 256-sample cuts and where a layer's off-ramp reaches 0, so how two layers'
    grains interleave depends on the block size. Each layer's own sequence, and the audio, are
    block-size invariant, and each golden renders at its spec's fixed block size, so the
    cross-platform gate holds; a test that compares logs across block sizes compares them per
    layer;
  - the determinism tests below, on each platform, which verify that renders are bit-exact on
    the same platform (§10);
  - the SIMD kernels against their scalar references, bit for bit;
  - for a change meant to leave the output alone (a refactor, or a feature at its defaults),
    every golden's WAV keeping its SHA-256 on one machine, before and after, stated in the commit
    or review. `scripts/update-goldens.sh` prints the hashes and lists the cases whose WAV
    changed since its previous run in that build tree.

  Level, Mix, the soft limiter and Space come after the grains, so only the metrics compare
  them, within 0.5 dB: a gain error there under about 6% passes every golden and, for a change meant to leave the
  output alone, the WAV check is what catches it.
- **Determinism tests.**
  - Block-size invariance: renders at block sizes 1, 32, 64, 511 and 4096, plus random splits,
    must be bit-identical, and so must a repeated render. Since M2 sub-project 3 the overload
    ladder has its own (16,384 one-second grains into one engine at a small quota: identical
    bytes and identical admitted, lowered, dropped and stolen counts at every block size; the
    full case is tagged `[slow]`, with a fast sibling that always runs), and so does the
    `Processor` with four layers at different Qualities over a small Budget, through
    `SerialRunner` and the stub pools. Since M2 sub-project 1 these include
    four-layer FX and instrument renders with every source kind switched in, every modulation
    source active and routes to every layer, and since M2 sub-project 2 (§23 #68) renders with a
    Resample layer, a loop with overdub, splices, and a `gg-render` capture at a frame off the
    control grid (block sizes 1, 77, 128, 1000 and 4096 for the capture). A ring resize or
    restore is part of such a render only when made before the first call, as `gg-render`
    makes them.
  - **Thread-pool invariance** (since M2 sub-project 1; §15, §23 #65): the four-layer FX and
    instrument renders, each layer switched between Sample, Live and an unbuilt kind and Feedback
    on two layers, render the same bytes, the same grains in the same order and the same ring
    heads through `SerialRunner` and through a stub pool of 1, 2 and 8 worker threads
    (`std::thread` workers and a barrier, off the audio path), at block sizes 1, 32, 64, 511 and
    4096; a pool that declines every other request renders the same bytes. The RTSan build runs
    the tasks on the stub's threads, its own lock and wait exempt as a stand-in for the host's
    pool (ruling LS-T11b). The stand-in for the in-host survey: the `[plugin]` tests' fake host
    pool checks that the plugin asks for exactly one task per rendering layer and renders the
    same bytes with the pool, with a declining pool and with none. Since M2 sub-project 2 a
    Resample layer fed back, a loop and overdub render the same bytes on any pool too.
  - Grain-event logs are compared across platforms.
- **Fuzz and property tests** (property tests on the state, the settings and the Processor
  since M1; the sweep harness in M2, **built in sub-project 3**: 30 `[sweep]` cases in
  `tests/sweep/`, 20 from sub-project 3, 3 from its follow-up and 7 from sub-project 4 (about
  26.5 s in `dev` and 194 s under ASan, timed when there were 20), run on every leg and under the
  sanitizers; §23 #66). Parameter sweeps, extreme values and bursts of automation
  must produce no NaNs, crashes, runaway gain, or clicks above a threshold.
- **CPU benchmark gate in CI:** a regression gate on in-process ratios, from M1's baseline. The
  §16 headline is a published measurement, not a CI gate. Through M1 and the 0.x releases the
  gate reports and never fails; it turns blocking at 1.0.0, after M5 (§23 #60, #61), with the
  published X.
- **Real-time safety.**
  - Clang RealtimeSanitizer on upstream LLVM 20 or newer (Apple clang doesn't ship it).
  - ASan and UBSan.
  - Tripwires that fail tests on allocation or locking in the audio path. Since M2 sub-project 2
    (§23 #68) they cover a Resample layer's piece, a loop with overdub, a splice command, a ring
    adoption with its bounded copy, a frozen restore, and the plugins' `process` and
    `params.flush` with a resize, a restore and a ring command pending.
- **Format validators in CI:** clap-validator, pluginval, auval, and Steinberg's VST3 validator.
- **Machine-readable artifacts.** Spectrogram images and summary statistics, so Claude can inspect
  renders and not just assert pass/fail.
- **The editor's tests** (since M2 sub-project 6; §23 #71).
  - The editor's math is framework-free in `gg-ui-core` (the lanes, the pitch axis, the scan
    rectangle and its hit test, the pad's law, the banded resolve, the layout table, the rings,
    the route edits and the undo stack), tested on every leg and under ASan, UBSan and RTSan.
  - The GUI tests (`[plugin]`) drive the real editor headless, through the plugins' CLAP
    factory, on every leg that builds the editor; the sanitizer presets build none. Each view
    opens, draws and closes in both products and both themes, at 960 × 600 and 640 × 360, where
    every control must lie inside its view; the gestures, the drops, undo through the gesture
    path, the theme setting, the keyboard and the About box each have theirs. Since PR 2 they
    sit in `canvas_tests.cpp` (the lanes, the menu, the marks), `panels_tests.cpp` (the panels
    and the FX view), `mod_view_tests.cpp`, `editor_media_tests.cpp` (the context's sample, ring,
    capture and cap API), `factory_tests.cpp` (the sample strip, the readout and the cap) and
    `editor_frame_tests.cpp`, over the shared `EditorTestUtil.h`.
  - **Every parameter is reachable from the UI,** by two machine checks: a `[ui]` test walks all
    283 slots (`kParamCount`) through the layout table, each placed
    exactly once, and a `[plugin]` test walks the built editor's views for each slot's control.
    A parameter a later wave adds fails both, naming it, until it is placed.
  - **`gg-snap`** draws every view as PNGs, headless (two products, four views, two themes, two
    sizes: 32 files). `snap_cli` runs it on macOS; CI's macOS leg uploads its output as the
    `gg-snapshots` artifact on every run, so the director and the reviewers can see the views
    without a host; the sub-project's listening pack (`docs/listening/m2-views/`) commits the
    960 × 600 set.
- **A human listening gate at every milestone.** From M2, one sitting at the milestone's close
  (§23 #64): each sub-project renders its pack for the record, and the director listens once.
- **In M2 the sub-projects are run by agent sessions** (§23 #64): a program-manager session
  launches one worker session per sub-project, in parallel within a wave; each worker writes its
  spec and plan and builds with task subagents in its own worktree; the program manager approves
  each spec against the program design and INTENT, and the director answers each spec's forced
  choices, decides behavior and sound, and hears the gate. A wave's sub-projects merge as each is
  ready, the later one rebasing on, or merging, the merged result and re-verifying (§23 #84,
  #88). **M3 runs as M1 did** (§23 #87): one session, its sub-projects in series, with parallel
  tasks inside each.
- **CI and release pipeline** (`docs/superpowers/specs/2026-10-29-release-pipeline-design.md`;
  §23 #61). One workflow, `ci.yml`: a `v*` tag, the nightly schedule or a dispatch with
  `installers: true` adds a `gg-installer` target per leg, packed from the very products that
  just passed every test and validator, and a `release` job. Adapted from Shortcircuit XT's
  GitHub Actions, sst-plugininfra's macOS installer script and sst-cmake's Inno Setup template.
  - macOS: a PKG with one component per format (CLAP, VST3, AU, the two apps and, since M2
    sub-project 5, the Content) inside a DMG,
    installed system-wide, signed with the director's Developer ID and notarized, with the
    certificates and an App Store Connect API key held as GitHub Actions secrets and never
    committed.
  - Windows: an Inno Setup installer (CI compiles it with Inno Setup 7.1.1, pinned and checked
    by SHA-256; §23 #90) and a plain zip, **unsigned at 1.0** (as Surge XT,
    Shortcircuit XT and OB-Xf ship); Azure Trusted Signing or SignPath Foundation's open-source
    program is the upgrade path once downloads make SmartScreen's warning matter.
  - Linux: a tar archive, built on Ubuntu 24.04, needing its glibc (2.39; §15, §23 #91).
  - Every installer carries `THIRD_PARTY_NOTICES.md`, the AGPL and GPLv3 texts, the platform
    SDK licenses and, since M2 sub-project 5, the factory content's CC0 text (`CC0-1.0.txt`), and
    each release attaches its complete corresponding source (the tagged tree
    with every submodule at its pin) beside the binaries, with the SDK tags clap-wrapper fetches
    named next to them.
  - A tag publishes a **draft** GitHub Release with SHA-256s; **the director publishes it by
    hand** after installing from the real DMG and EXE and running the host smoke in Live and
    Bitwig. The nightly builds and signs the same installers as run artifacts, and from M5 on
    publishes them to a rolling `nightly` pre-release (a committed switch). A pull request or a
    push to `main` builds no installer and sees no secret.
- **Versioning.** Semantic versions numbered by milestone: M2 is tagged 0.2.0, M3 0.3.0 and M4
  0.4.0, each a draft release the director keeps unpublished, for host testing and rehearsal
  (nothing is public and there are no betas before M5; §23 #62); M5's `0.5.0-beta.N` are the
  first public releases, then 0.5.0, then 1.0.0; a fix to a superseded minor comes from a
  `release/0.x` branch cut at its tag. The version lives in `CMakeLists.txt` alone, the
  numeric part in `project()` and the pre-release suffix beside it, `dev` by default, so `main`
  reports `0.2.0-dev` and never claims a release; a nightly reports `-dev.<yyyymmdd>.g<sha7>`.
  The `plan` job fails a tagged run before anything is built or signed if the tag and the CMake
  version disagree or the suffix is `dev`.

### 20. Roadmap

| Milestone | Delivers | Exit criteria |
|---|---|---|
| **M0 Foundations** (done 2026-10-26) | Repo, CMake, CI matrix (mac/win/linux), `LICENSE` (AGPL-3.0); headless engine skeleton; `gg-render`; test, golden-render, RTSan and benchmark harness; both plugin shells in every format plus standalone; validators in CI. **Spikes:** kernel benchmark (sets §16), JUCE 9 vs 8.0.16, two-descriptor CLAP to AUv2/VST3/standalone, Ableton MIDI routing into an effect, CPU canvas frame budget. | One grain stream renders deterministically. The empty plugins load in the first-class hosts and pass every validator. **Met** (§23 #29). |
| **M1 Cloud core** (closed 2026-10-29; `docs/m1-carry-over.md` is its frozen record, whose 13 open rows moved to `docs/m2-carry-over.md`, §23 #63) | Six sub-projects, each spec → plan → build → review (§23 #35): 1 FX vertical slice (`docs/superpowers/specs/2026-10-27-m1-fx-slice-design.md`); 2 Cloud engine; 3 Sources; 4 Space; 5 Perform UI; 6 Close. Together they deliver: grain pool and SIMD renderer (Eco/Normal; Eco is engine-only until M2, §23 #43), the §16 lever list (§23 #39); Cloud scheduler; parameter model; Sample and Live sources with freeze; note-triggered freeze/capture in the FX (§23 #32; built in sub-project 3 as hold and play, a note freezing the ring and repitching the grains, §23 #48); 1 layer; diffusion/reverb (built in sub-project 4 as Space: a per-layer diffuser and a patched Reverb 2 with an infinite Decay, §23 #50); state save/load; single-lane canvas plus 8 macros | Both plugins make textures. **Listening gate #1: passed 2026-10-29** on the Sources, Space and Perform packs (`docs/listening/`; §23 #60). CPU baseline recorded; the CPU gate reports without failing through M1 and the 0.x releases, and turns blocking at 1.0.0, after M5 (§23 #44, #60, #61). The §16 headline measured on the reference core and published: **19.5%** at block 256 (§23 #39, #60). **M1 closed 2026-10-29** by sub-project 6 (§23 #60; the carry-over's 13 open rows name what they wait on). clap-validator runs in full: M0 skipped its `param-conversions` test, which panics on a plugin with no parameters (ruling T8-c, free-audio/clap-validator#40); sub-project 1 removed the exclusion (§23 #35). |
| **M2 Giga** (closed 2026-11-03, PR #31, `docs/superpowers/specs/2026-11-02-m2-close-design.md`, §23 #93; in progress since 2026-10-29; its carry-over: `docs/m2-carry-over.md`, 13 open rows from M1 at its start, 15 open at the close) | **Seven sub-projects in four waves** (`docs/superpowers/specs/2026-10-29-m2-program-design.md`, §23 #64), each spec → plan → build → review, run by one agent session each, in parallel within a wave: wave 1, **1 Layers and structure** (alone; built 2026-10-30, PR #15, merged; §23 #65); wave 2, **3 Engine scale and quality** (built 2026-10-30, PR #16 and its follow-up PR #23, merged; §23 #66), **2 Sources and persistence** (built 2026-10-30, PR #19, merged; §23 #68), **4 Extremes and the FX chain** (built, PR #20, merged; §23 #69; merged in that order); wave 3, **5 Presets and content** (group 1 built 2026-10-31, PR #17; group 2, the 48 factory presets and content v2, built 2026-11-01, PR #25; both merged; §23 #70), **6 Views** (PR 1 built 2026-10-30, PR #18; PR 2 built 2026-11-01, PR #24; both merged; §23 #71); wave 4, **7 Close** (built 2026-11-03, PR #31; §23 #89 to #93). Together they deliver: 4 layers plus morph; budget manager; no CPU regression vs the M1 measured headline (§23 #41); mipmaps and the HQ sinc tier; minutes-long buffers with persistence, and the live buffer's operations (overdub, loop length, splices, record/capture; §4, §5, §23 #64); Smear and Spectral Smear; resample/feedback source; the per-layer filter, grain buses, global delay and limiter (§14); full mod matrix; Mutate; factory presets and CC0 samples, including MIDI-driven FX presets (§23 #32); preset discovery; the Edit, Mod and FX views and the completed Perform view (§17); C++23 and the AVX2 spike (§22); a timeboxed GPU-canvas spike (§17, §23 #26). **Tagged 0.2.0**, a draft release kept unpublished: no beta, nothing public before M5 (§23 #62) | Benchmarks: the report-only gate shows no regressed ratio against the committed baselines (M1's on `mac-universal`; on x86 the entries remade at the AVX2 task, §23 #66), and X within 3% of M1's published headline on the reference core (§23 #41). The 22 M1 goldens keep their WAV hashes unless a commit says why one moved. **Listening gate #2,** one sitting at the close (§23 #64). **Met 2026-11-03** (§23 #92, #93): X is 19.4% at block 256 against M1's 19.5% (ratios +1.4% / +1.6% / +0.6%, inside 3%); the report-only gate was read at the close (`docs/benchmarks/README.md`); the 22 M1 goldens keep their WAV hashes; all 40 gate items are answered and the eight host cells passed. |
| **M3 Playable** (in progress since 2026-11-07, `docs/superpowers/specs/2026-11-07-m3-program-design.md`, §23 #94) | Six sub-projects in series, each spec → plan → build → review, in one session (§23 #87): 0 carry-over row 405 first; 1 **Voice slice** (a per-layer Mode, Cloud or Voice; voices with their own streams, envelopes and a release that keeps spawning; MPE and CLAP note expressions; the sustain pedal); 2 **Full expression**; 3 **Pitch-sync and formant grains**; 4 **Microtuning**; 5 **Views and presets**; 6 **Close**. Together they deliver: Voice scheduler, polyphony, MPE, note expressions, polyphonic modulation, per-voice envelopes, pitch-sync/formant grains, microtuning. **Tagged 0.3.0**, unpublished | Listening gate. First-class hosts pass. |
| **M4 Rhythmic** | Sync scheduler: grid, swing, probability, patterns, transport-stop/loop policies. **Tagged 0.4.0**, unpublished | Listening gate. Sync tests pass under loops and jumps. |
| **M5 Synthesis and extremes** | Oscillator sources, synthetic grains, per-grain filters. Full theme support (§23 #72): a System choice that follows the OS, a high-contrast theme, more built-in themes, and user theme files. **Ships as 0.5.0: its betas, `0.5.0-beta.N`, are the first public releases,** and identities and parameter IDs freeze at the first (§18); the public nightly starts here (§19, §23 #61, #62) | Listening gate. Budget weights calibrated. |
| **→ v1.0.0** | M0 to M5 polished (§23 #61; v1.0 followed M2 until 2026-10-29) | Success criteria (§10) met. Factory preset order frozen; the CPU gate turns blocking (§16, §19). |
| **M6 Reach** | iOS AUv3 and/or LV2, accessibility polish, community infrastructure | Target-specific validation passes. |

Why this order before v1.0:
- **Playable comes first** because it gives the instrument its own identity, and the Voice
  scheduler is groundwork for Sync.
- **Synthesis** is the largest new surface.
- **Reach** waits until the shells are stable.

### 21. Risks

| Risk | Mitigation |
|---|---|
| Older and x86 machines pay 2× to 3× the M5 Max's CPU share for the same grain count | The four §16 levers, judged on every CI leg; the informative floor figures (§16); the budget manager; the quality tiers |
| Determinism is fragile: one CPU-adaptive rule, `std::` distribution or thread-order sum silently breaks it | Rules in §14; block-size invariance tests from M0 |
| Multithreading is uneven across formats; there's no workgroup join via CLAP/clap-wrapper | Single-threaded v1; tasks only when the host offers a thread pool |
| Memory blow-up: layers × minutes × mipmaps × instances | As built in M2 sub-project 2 (§14, §23 #68): a per-instance memory cap (default 1 GiB, 64 MiB to 16 GiB) that refuses a load, a Ring Length or a restore deterministically, with a status, before anything is allocated, shown in the UI since M2 sub-project 6's PR 2; a process-wide sample pool holding each prepared sample once, weakly; rings of 1 to 300 s; the fixed costs documented. Buffer resolution options remain a later lever. **The pyramid's cost, as built** (sub-project 3): one copy per level takes a stereo file from 2× to 3× its audio and a mono one from 1× to 2×, so a 10-minute stereo 48 kHz file is about 690 MB where it was 460, built for every file at load whether or not an HQ layer uses it (Quality changes on the audio thread, which can't build it); `PreparedSource` reports its bytes for sub-project 2's cap, which counts the levels |
| Large state chunks slow or crash hosts | 32 MB embed cap (float32 WAV payloads in `AUDI`, then the captures folder; M2 sub-project 2); a 64 MiB state read cap; a frozen ring's fingerprint cached, so a host saving every few seconds hashes it once; background decoding for interactive loads (a session restore loads its samples and rings synchronously, D8, §18) |
| Clicks and level pumping at extremes (head crossing, normalizing thousands of grains) | Engine invariants in §14; click detector in the golden tests |
| sst API churn (no releases); clap-wrapper bugs (transport, AU tail) | Pinned SHAs; upstream fixes; host smoke tests |
| JUCE 9 compatibility of the shim | The M0 spike passed on macOS arm64, so the project is on 9.0.4 (§23 #24). CI run 40445561691 passed on Windows, Linux and macOS (§23 #29), and the macOS leg now also runs the mac-universal x86_64 slice's tests under Rosetta (§23 #33). M1 and M2 run on 9.0.4; 8.0.16-security is no longer a fallback (§15) |
| Crowded market, including free granular built into DAWs (Granulator III, Bitwig Fragments) | Differentiate on scale, the canvas, openness and portability, not on feature-count parity |
| Open-source granular projects tend to stall | Tight v1 scope around the texture anchor; a published roadmap; verification that lets Claude-led work proceed safely |
| Windows builds are unsigned at 1.0, so users see SmartScreen's unknown-publisher warning once (SignPath's free terms may exclude JUCE's dual license) | Decision 3 of §23 #61: ship unsigned, as the surge-synthesizer projects do; move to Azure Trusted Signing (about $10 a month) or SignPath once downloads make the warning matter |

### 22. Open questions

| Question | Resolved in |
|---|---|
| ~~Plugin identities: CLAP IDs (proposed `org.sethbc.giga-grains`, `org.sethbc.giga-grains-fx`), AU manufacturer code (at least one uppercase letter) and subtypes. Frozen at first release.~~ | **Resolved** in M0 (2026-10-26): see §18 and §23 #20 |
| ~~JUCE 9.0.4 vs 8.0.16-security for the GUI shim~~ | **Resolved** in M0 (2026-10-26): JUCE 9.0.4; see §15 and §23 #24 |
| ~~How Ableton Live routes MIDI into VST3/AU effects (sets how prominent MIDI-driven FX features are)~~ | **Resolved** in M0 (2026-10-26): a MIDI track's "MIDI To" reaches Giga Grains FX as VST3 and as AU, and MIDI-driven FX features are first-class; see §23 #25 and `docs/spikes/m0-ableton-midi-fx.md` |
| ~~Whether the CPU canvas holds its frame budget, or a GPU backend is needed~~ | **Resolved** in M0 (2026-10-26): it holds (under 3 ms of UI-thread time at 8,192 grains in both first-class hosts), and the GPU backend spike stays in M2; see §16, §17 and §23 #26. **The M2 spike resolved the GPU backend** (2026-10-30, M2 sub-project 6): the CPU renderer stays. JUCE OpenGL and Metal each beat it by 6.6× to 45×, measured interleaved at one contended load, but OpenGL is deprecated on macOS and went unmeasured in hosts, under Rosetta, on Linux and on Windows, and Metal is macOS-only; Metal is the macOS lever if the budget ever breaks (`docs/spikes/m2-gpu-canvas.md`; §17, §23 #71) |
| Which hosts besides Bitwig accept CLAP thread-pool requests under load | M2 sub-project 1 built the tasks and the pool support, with a fake host pool as the machine-checked stand-in (§15, §19, §23 #65); the in-host survey stays sub-project 7's (the close's) checklist (§23 #64), a cell of its host matrix (§23 #86) |
| ~~Whether grain buses with channel masks are enough for v1 sound design, or per-grain filters are needed sooner~~ | **Resolved** in M2 sub-project 4 (§23 #69) by a bench-only kernel, `hermite-simd-grainfilter` (`hermite-simd` plus one `CytomicSVF` per grain, stepped per output sample, on the M0 workload). Its cost is an upper bound: the whole SVF per grain, two of four lanes used and the scratch clear counted. At the idle run of 2026-11-01 (`docs/benchmarks/2026-11-01-m2-extremes.json`) it took 8.919 / 9.043 / 9.400 ns per grain-sample at blocks 32 / 64 / 256, 4.18× / 4.38× / 4.71× `hermite-simd`'s and 3.61× / 4.03× / 4.55× X's; the filter's own added cost (the kernel less `hermite-simd`) is 6.79 / 6.98 / 7.40 ns, 2.75× / 3.11× / 3.59× X per grain-sample. Per the spec's rule, above a quarter of X per grain-sample buses with channel masks are v1's answer and per-grain filters wait for M5's budget weights (§14), and below it M5's decision starts from the number. The added cost is far above a quarter of X (2.75× to 3.59×), so buses with channel masks are v1's answer and per-grain filters wait for M5's budget weights |
| A global DC blocker beside the delay and limiter (§14 lists one with the soft limiter): nothing needs it yet, and the reverb's ∞ has its own | Post-M2, when a source needs it (§23 #69) |
| Feedback into a Spectral layer's material: the layer's Feedback is 0 (its ring is its material), so feeding the smear back is a later option, not a default surprise | Post-M2 (§23 #69) |
| Logic Pro's MPE and tail behavior with the wrapped AUv2 | M3 |
| Default behavior of rhythmic streams when the transport stops: keep running or hold | M4 |
| An optional "Classic" Clouds-faithful mode using sst-effects' Nimbus | Post-v1 |
| ~~Moving from C++20 to C++23: worth it for `std::expected` (the media loader's and state decoder's error paths) and smaller conveniences, but gcc-12's library support is partial and Apple's libc++ gates some C++23 library features by macOS version (we target 11.0); no audio or determinism effect~~ | **Resolved** on 2026-10-30, by M2 sub-project 1's first task: C++23, with a GCC 13 floor (gcc-13 from the `ubuntu-toolchain-r/test` PPA on ubuntu-22.04), green on every CI leg with no revert; see §15 and §23 #65 |
| ~~Wider x86 SIMD (AVX2, 8-wide) for the grain kernels: today x86 targets `-march=nehalem` (SSE4.2) and the macOS x86_64 slice Apple's default, with 4-wide kernels everywhere (NEON on Apple Silicon gains nothing). It would need two compiled kernel paths chosen at load by CPU features with an SSE fallback, bit-identical output between paths (as the SIMD kernels already are to the scalar reference; no FMA), a ruling that choosing a path by CPU features (with identical output) doesn't breach §14's no-CPU-adaptive-behavior rule, and rebuilt x86 `gg-bench` baselines. Measure first, as sub-project 2 did its levers | A timeboxed task in M2 sub-project 3, after the HQ kernel, kept only if the CI floor legs' in-process ratios improve beyond their spread (§23 #64)~~ | **Resolved, kept** on 2026-10-30 (`docs/spikes/m2-avx2.md`; §14, §16, §23 #66): `engine-sample` beat `engine-sample-sse` by 13% to 35% in all 18 leg-run-block cells of three full-length runs, clearing the spread by 2.5 to 13 times, and the 8-wide path renders the SSE path's bits |
| Splitting a layer's grains into further render tasks (layer × grain chunk) for the thread pool: it re-associates the float sum, so it moves every golden once and changes the bits of a one-layer render; the gain is for a one-layer instance in a host with a thread pool. Weigh it when such hosts matter (§15) | After M2 (§23 #64) |
| The rest of §14's per-grain spread model: Texture spread, the other fields' spreads, and the spread distribution selector (with scale or grid quantize) | After M2: the director's D5 chose Pitch Spread and Size Spread for M2, and wave 3 adds no parameters (§14, §23 #65) |
| ~~Windows signing route and budget~~ | **Resolved** on 2026-10-29: Windows ships unsigned at 1.0 (route 1, as Surge XT, Shortcircuit XT and OB-Xf do); Azure Trusted Signing or SignPath later; see §19, §21 and §23 #61 |
| ~~Windows standalone ASIO: it's off, because RtAudio's bundled Steinberg ASIO files have no license grant (§23 #30). Re-enable it only from a Steinberg ASIO SDK release whose license is AGPLv3-compatible (Steinberg's GPLv3 release). Such a release now exists: ASIO SDK 2.3.4, licensed "Steinberg ASIO License or GPLv3", which sst-plugininfra bundles (§15, §23 #38). Whether to re-enable ASIO stays open~~ | **Resolved** on 2026-10-28: ASIO is on in the Windows standalone, from ASIO SDK 2.3.4 under its GPLv3 option; see §15 and §23 #56 |

### 23. Decision log

Rows 1 to 18 were decided on 2026-10-26, in the founding brainstorm between the director and
Claude. The research behind them covered frameworks, libraries, granular DSP, the competitive
landscape, and host and UI integration. Each area was adversarially fact-checked against primary
sources. Rows 19 onward were decided during the milestones; each says when, and by whom when the
director made the call.

| # | Decision | Why / notes |
|---|---|---|
| 1 | Open-source release | Community-facing project in the Surge-ecosystem tradition |
| 2 | All four musical focuses are core; **evolving textures** is the v1 anchor | One engine serves all four; textures exercise both plugins |
| 3 | Two plugins, one engine | Cleanest DAW integration; AU can't be both instrument and effect |
| 4 | Sources: samples, live input, oscillators, resampling | Full creative range; uniform read interface |
| 5 | "Giga" = thousands of grains, minutes-long buffers with extreme stretch, 4 layers | Defines the differentiator and the performance architecture |
| 6 | macOS, Windows, Linux and standalone; design for iOS AUv3 and LV2 | Broad reach; extra targets shouldn't constrain v1 |
| 7 | References: Clouds/Beads/Morphagene, Portal/Fragments/Microcosm, Borderlands/Emergence, Quanta/Granulator III | The director wants the best of each |
| 8 | Primary UI = live grain canvas plus macro row | Immediacy and truthfulness; depth lives in secondary views |
| 9 | Claude-led development; the director directs, listens and reviews | Makes automated verification first-class |
| 10 | Deliverable shape: `INTENT.md` plus `CLAUDE.md` | A north star for future sessions |
| 11 | Brief confirmed: product names; v1 = both plugins centered on texture; 4 layers; success criteria | §2, §5, §10 |
| 12 | Principles, non-goals, engine architecture, verification strategy, UI and roadmap approved | §8, §9, §14, §17, §19, §20 |
| 13 | First-class hosts: Ableton Live 12 and Bitwig Studio | The director's daily DAWs |
| 14 | Factory content: a small curated CC0 sample set plus presets | The instrument shouldn't open silent in v1 |
| 15 | Language: C++20 | Ecosystem (CLAP, clap-wrapper, sst, JUCE); real-time control; a working local template |
| 16 | Stack: CLAP-first with clap-wrapper; JUCE for the GUI only | CLAP-native features without JUCE's AudioProcessor; JUCE 9 has no native CLAP; follows Shortcircuit XT |
| 17 | License: AGPL-3.0-or-later for everything | Matches what the binaries are anyway (JUCE AGPL plus GPL-3 sst); one simple story |
| 18 | Research corrections adopted | Gap restated as depth + scale (§1); scale stated per layer and per instance (§5); live-buffer differentiator (§5); AAX moved to "possible later"; the FX registers both `aufx` and `aumf`; clap-wrapper ≥ 0.17 floor; start on JUCE 8.0.16-security; RTSan needs upstream LLVM; SignPath is uncertain |
| 19 | Grain pool uses stable compaction, not swap-remove | Float summation order must not depend on block boundaries (§8.6). Decided 2026-10-26, during M0; see §14 |
| 20 | Plugin identities fixed | Vendor `sethbc`; URL `https://github.com/sethbc/giga-grains`; instrument CLAP ID `org.sethbc.giga-grains` ("Giga Grains"); FX CLAP ID `org.sethbc.giga-grains-fx` ("Giga Grains FX"); AU manufacturer `Sbcx` (display name `sethbc`); instrument AU `aumu`/`GGin`/`Sbcx`; FX AU `aufx`/`GGfx`/`Sbcx`, plus the extra identity `aumf`/`GGfx`/`Sbcx` through clap-wrapper's legacy-identity factory. Frozen at the first public release. Decided 2026-10-26, during M0; see §18 |
| 21 | M0 tooling dependencies: dr_libs, pffft, stb_image_write, nlohmann/json, CLI11 | Audio-file decoding, the metrics FFT, spectrogram PNGs, metrics and render-spec JSON, and the `gg-render` command line, for the golden-render tooling (§19). All AGPLv3-compatible (§12). Decided 2026-10-26, during M0; see §15 |
| 22 | §16's first-principles kernel cost estimates replaced by measured M5 Max numbers; headline target unchanged | The M0 kernel benchmark (`gg-bench`, 2,000 grains, median of 5 runs) puts the 4-wide SIMD Hermite kernel at 21.6% / 20.7% / 19.9% of one M5 Max core at blocks 32 / 64 / 256. That is under the 25% target, so the stop rule didn't trip. The headline is judged on the SIMD Hermite kernel (ruling T7-a). The M5 Max is the ceiling, so the M1-runner and x86 numbers from CI (M0 Task 10b) decide whether the target holds; see #23 for what they showed. See `docs/spikes/m0-kernel-benchmark.md`. Decided 2026-10-26, during M0; see §16 |
| 23 | §16 headline kept as a provisional target despite the CI miss; milestone M1 closes the gap | Full-length `gg-bench` in M0 CI (run 40439674760, M0 Task 10b) puts the SIMD Hermite kernel at blocks 32 / 64 / 256 at: 52.9% / 51.9% / 48.0% of one core on GitHub's Apple M1 VM; 59.7% / 56.9% / 54.9% on a Windows AMD EPYC 7763 VM; and 69.2% / 63.5% / 60.1% on a Linux one. That is about 2× the 25% budget (1.9× to 2.8×), and on the Linux VM small blocks cost relatively more than on the M5 Max. The director chose to keep the headline as a provisional target and to make closing the gap an explicit goal of the milestone-M1 renderer, with full-length CI runs (Task 10b, the run above) quantifying the gap. The candidate levers are per-grain software prefetch, a stereo-interleaved source layout and cache-aware grain scheduling. M1's CI CPU gate starts as a regression gate at the measured baseline, and the headline is judged again at the end of M1. See `docs/spikes/m0-kernel-benchmark.md`. Decided 2026-10-26 by the director, during M0; see §16, §19, §20 and §21. Superseded by #39 |
| 24 | Start on JUCE 9.0.4, not 8.0.16-security | The M0 spike built `clap_juce_shim`, the editor and both plugins against JUCE 9.0.4 (`3fb5cbc`) with no source changes and no new warnings. On macOS arm64, every test and validator passed: clap-validator, pluginval at strictness 10 with GUI tests, auval for all three identities and Steinberg's VST3 validator. The live editor looked right. The license is unchanged: AGPLv3 or commercial, so it is AGPLv3-compatible (§12). Linux CI now installs `libxi-dev`, because JUCE 9 includes XInput2. JUCE's WebP support is off (`JUCE_USE_WEBP=0`, in `ui/CMakeLists.txt`): 9.0.4's libwebp SSE4.1 unit doesn't compile under ClangCL (the AVX2 units use the same pattern but weren't seen failing), and we never load WebP. That bug is worth reporting upstream to JUCE; the director decides. sst-jucegui (main) also builds and runs on 9.0.4. Windows, Linux and the mac-universal x86_64 slice are left to CI, and 8.0.16-security is the fallback. (Since then CI run 40445561691 passed on every leg, #29, and CI runs the x86_64 slice, #33.) Supersedes "start on JUCE 8.0.16-security" in #18. See `docs/spikes/m0-juce9.md`. Decided 2026-10-26, during M0; see §15, §21 and §22 |
| 25 | MIDI-driven FX features are first-class | The M0 spike routed a MIDI track into Giga Grains FX in Ableton Live 12.4.7: the MIDI track's "MIDI To" set to the FX's audio track, then to Giga Grains FX. The editor's note-on counter rose with the VST3 and with the AU. For the AU, that is at least one of the FX's two identities; which one Live used wasn't recorded. The director: MIDI-driven FX features are first-class; Voice mode, MIDI pitch-sync and note-triggered freeze/capture are headline FX features, in the main UI and the factory presets; the FX's presets and docs explain MIDI routing for each host. #32 places them on the §20 roadmap. See `docs/spikes/m0-ableton-midi-fx.md`. Decided 2026-10-26 by the director, during M0; see §22 |
| 26 | The GPU canvas backend spike stays in M2 | With 8,192 grains at the display's native scale, the editor's "frame ms (avg 60)" stayed under 3 ms in Ableton Live 12.4.7 and in Bitwig Studio 6.1.5 on the M5 Max. No exact numbers were recorded. On macOS the readout is UI-thread time only, because JUCE draws the view's layer asynchronously, so it is a lower bound on the whole frame. It does meet the §16 budget as worded. Floor hardware, Windows, Linux and canvases larger than the editor's default weren't measured. The CPU splat renderer stays the v1 renderer. See `docs/spikes/m0-canvas-budget.md`. Decided 2026-10-26 by the director, during M0; see §16, §17 and §22 |
| 27 | Listening gate #0 passed | The four golden renders (`sparse-sine`, `sparse-sine-44k1`, `dense-noise`, `sweep-stretch`) were regenerated at `c7e3a1a`, with the dev suite at 79/79. The director: "sparse-sine pulses clearly, no clicks, 44k1 matches, sweep is steady". The objective checks: `sparse-sine` and `sparse-sine-44k1` have 24 grains, with onsets every 125.0 ms and 52% / 53% silence. `dense-noise` has about 20 overlapping grains and is continuous by design, so its +7 semitones can't be judged by ear on noise; every grain's rate in the event log is 1.4983070768766813 = 2^(7/12). `sweep-stretch` has about 6 overlapping 200 ms grains at rate 0.5; its output's zero-crossing frequency is about 1210 Hz, against about 2.25–2.55 kHz in the source at position 0.75 of the exponential 50 Hz → 8 kHz sweep, which confirms −12. Decided 2026-10-26 by the director, during M0; see §13 and §19 |
| 28 | §18 records the derived identities and the parameter ID range | The VST3 class UIDs are `56E869BFEDD75019BD0C3105FA80B835` (Giga Grains) and `DBAF5C12EA095FEFB3A5CBBBB254442A` (Giga Grains FX). clap-wrapper derives them from the CLAP IDs as version-5 UUIDs, and they match Ableton Live's scanner log. The standalone app bundle IDs are `org.sethbc.giga-grains.standalone` and `org.sethbc.giga-grains-fx.standalone` (ruling T8-a). Ruling T8-d: our own CLAP parameter IDs stay below `0xB00000`, because clap-wrapper's 2,096 VST3 MIDI-mapping parameters start there; hosts may save those IDs, so they are effectively frozen and depend on clap-wrapper's layout. All of these are frozen at the first public release, with the identities in #20. Decided 2026-10-26, during M0; see §18 |
| 29 | M0 Foundations complete | Both exit criteria are met. **One grain stream renders deterministically:** the determinism tests pass (bit-identical output at block sizes 1, 32, 64, 511 and 4096, under random splits, and across repeated renders), and the golden event logs match exactly, line by line (CR and final newline ignored), on macOS, Windows and Linux in CI. **The empty plugins load in the first-class hosts:** both plugins pass in Ableton Live 12.4.7 (VST3 and AU) and Bitwig Studio 6.1.5 (CLAP and VST3). Live listed the VST3 only once the bundle was signed as a whole; `05731d1` now signs every macOS bundle at build time (`docs/spikes/m0-host-matrix.md`). **They pass every validator in CI:** clap-validator (without `param-conversions`, ruling T8-c), pluginval at strictness 10, auval for all three AU identities, and Steinberg's VST3 validator. CI run 40445561691 on `933bfda` (JUCE 9.0.4) passed on all three build legs and the sanitizers. Listening gate #0 passed (#27). Running clap-validator in full becomes an M1 exit criterion. Decided 2026-10-26, during M0; see §20 |
| 30 | The Windows standalone builds without ASIO | RtAudio 6.0.1, which clap-wrapper fetched for the Windows standalone's ASIO support, bundles Steinberg's ASIO SDK files (`include/asio*`, `iasiodrv.h`). They carry Steinberg's copyright and no license grant, and CLAUDE.md allows no unlicensed code, so `libs/CMakeLists.txt` now sets `RTAUDIO_API_ASIO` off (final-review ruling F4). With ASIO off, clap-wrapper doesn't build RtAudio on Windows at all: the standalone keeps its own WASAPI audio and WinMM MIDI backends. RtAudio, RtMidi and wil, which clap-wrapper's standalone can fetch, are now listed in §15 with their licenses and pins. ASIO comes back only from a Steinberg ASIO SDK release under an AGPLv3-compatible license, before the first distributed Windows build (§22). Decided 2026-10-26, during M0 (final review); see §15 and §22 |
| 31 | No floating-point contraction, recorded in §14 | Every first-party target has compiled with `-ffp-contract=off` (ClangCL: `/clang:-ffp-contract=off`) since M0 Task 1, so `a * b + c` is never fused into an FMA on one platform and rounded twice on another; that is part of what keeps the grain-event logs identical across platforms. Until now the rule lived only in the M0 plan's global constraints. Decided 2026-10-26, during M0 (final review); see §14 |
| 32 | First-class MIDI-driven FX features placed on the roadmap | #25 made Voice mode, MIDI pitch-sync and note-triggered freeze/capture headline FX features but placed none of them in §20, so as written v1 shipped no MIDI-driven FX feature. The director's decision: note-triggered freeze/capture is added to M1, which already builds freeze; MIDI-driven FX factory presets are added to M2; Voice mode and pitch-sync stay in M3. Decided 2026-10-26 by the director, during M0; see §20 and #25 |
| 33 | M0 final-review corrections to Part II | The M0 final review's fix wave corrected Part II without changing a decision: §15 and §21 now give CI run 40445561691's JUCE 9 result (every leg green, Windows ClangCL included), and the macOS CI leg now runs the mac-universal x86_64 slice's unit tests and golden checks under Rosetta; #24 says only the SSE4.1 libwebp unit was seen failing; #23, §21 and `docs/spikes/m0-kernel-benchmark.md` scope the small-block cost to the Linux VM and record that full-length CI runs quantify the gap; §20's M1 exit criteria name the §16 headline re-judgment that #23 already decided; #25 keeps only the director's words; #29 says the event logs match exactly per line, not byte for byte; §18 notes clap-wrapper's VST3 preset selector at `0xC00000`; and the preamble above no longer dates every row to the founding brainstorm. Decided 2026-10-26, during M0 (final review); see §15, §18, §20, §21 and #23 to #29 |
| 34 | M0 hands M1 a carry-over list | Everything the M0 reviews deferred to M1 is in `docs/m1-carry-over.md`, by area, each with its source and, where a reviewer gave one, its deadline; §20's M1 row links it. It replaces the review workspace, which is deleted when M0 closes (final-review ruling F3). Its first item, before parameters land: a JUCE-free plugin core, so the asan and rtsan presets cover the plugin audio path. Decided 2026-10-26, during M0 (final review); see §20 |
| 35 | M1 is built as six sub-projects, an FX vertical slice first | M1 is too large for one spec, so it is split into six sub-projects, each going through spec → plan → build → review: 1 the FX vertical slice; 2 the Cloud engine (SoA pool, SIMD Eco/Normal kernels, a gate-grade `gg-bench`); 3 Sources (the Sample source, note-triggered freeze/capture, the media manifest); 4 Space (diffusion and reverb); 5 the Perform UI (canvas, 8 macros, Texture, Feedback); 6 Close (listening gate #1, the CPU figure, the host matrix). The director's three decisions: **a thin vertical slice first**, not risk-first or two parallel tracks, to put sound in front of ears early with final interfaces; **the first sound is the FX on live input**, with no file I/O and no new UI, the shortest path to a texture in Ableton Live and Bitwig (the instrument gets its sound in sub-project 3); **a shared, JUCE-free core** that holds the patch, the parameter mapping and the FX processing, used by the plugin and `gg-render`, so the goldens test exactly what a DAW plays. Sub-project 1 also removed clap-validator's `param-conversions` exclusion, now that the plugins have parameters (`a8202e2`; ruling T8-c). See `docs/superpowers/specs/2026-10-27-m1-fx-slice-design.md` §1. Decided 2026-10-27 by the director, during M1; see §20 |
| 36 | The Cloud core's DSP rules, recorded in §14 | What M1 sub-project 1 built, and the rulings its reviews made: the 32-sample control grid on absolute samples, with a boundary at a call's end applied in the next call (M1-u) and a Mix/Level ramp restarted only when its target changes (M1-s); the Cloud scheduler's recurrence `Δ(k) = max(P × ((1 − d) + d × E(k)), 1/16 sample)` (M1-g) with the deterministic `logDet`; Density ≤ 0 is off, positive values clamp to [0.01, min(4000, sr)] (M1-a); a Density or Distribution change rescales the pending onset from the same draw (M1-h), which supersedes the slice spec's "next onset computed after its grid point"; Position and Spray in seconds in Live mode (M1-k); per-grain equal-power pan with `√(1 ± pan)` gains, which supersedes the slice spec's "polynomial sin/cos"; the latched gain `1 / √max(1, Density × Size)`; the soft limiter `1 − 0.25/|x|` above 0.5 (M1-t); a dry path that zeroes non-finite samples and passes finite ones unclamped (M1-q); a live ring stored in N + maxBlockSize frames (M1-o), with an admission clamp exact through the 20 ms freeze ramp and input clamped to ±2^20 (M1-p), and a 256-frame head-proximity fade. Each keeps output bit-identical at any host block size. Decided 2026-10-27, during M1 sub-project 1; see §14 |
| 37 | Parameter IDs: `(scope << 16) \| index`, and the first ten | Scope 0 is global and scopes 1 to 4 are layers A to D, so M2 adds layers without renumbering; index 0 is reserved; every ID is below `0xB00000` (ruling T8-d) and append-only. The ten: Mix `0x000001`, Level `0x000002`, and layer A's Position, Size, Density, Distribution, Pitch, Spray, Stereo and Freeze, `0x010001` to `0x010008`. Both products expose the same list and share one state format; the instrument hides Mix and leaves its remote-controls slot empty (ruling M1-w). The state is `gg-core`'s versioned XML (version 1) with each value's float bits and the engine seed. The IDs are pinned by a test and frozen at the first public release, with the identities in #20. Decided 2026-10-27, during M1 sub-project 1; see §18 |
| 38 | `gg-core`, `GG_BUILD_EDITOR` and sst-plugininfra | `gg-core` (`core/`) is the JUCE-free layer between the engine and the plugin: the patch and its state, the parameter mapping and the `Processor`, shared by the plugin and `gg-render`. The plugin splits into a JUCE-free core and an editor layer; `GG_BUILD_EDITOR` (default ON) builds the editor and the clap-wrapper formats, and the `asan` and `rtsan` presets build with it OFF, so the plugin audio path runs under ASan/UBSan and RealtimeSanitizer, which closes the carry-over's first item (`ec2a054`). sst-plugininfra (MIT) is pinned at `main` @ `8e4271c`; only its `patchbase` target and bundled tinyxml (zlib) are built, private to `gg-core` (ruling M1-l). Its bundled `libs/asiosdk` is ASIO SDK 2.3.4, licensed "Steinberg ASIO License or GPLv3"; it is unused and never built. §22 records that a GPLv3 ASIO SDK release now exists; re-enabling ASIO stays open, and #30 stands. Decided 2026-10-27, during M1 sub-project 1; see §15 and §22 |
| 39 | §16 headline measured on the M5 Max and published as measured, with no ceiling; floor figures informative; sub-project 2 tries four levers and keeps the winners | M0 measured the SIMD Hermite kernel at 20–22% of one M5 Max core, 48–53% on CI's M1 VM and 55–69% on the EPYC VMs. #23's levers attack cache misses and per-grain overhead, which the M5 Max barely shows, so nothing guaranteed they would close 2×, and sub-project 2 would have opened with a bar it might not meet. The decision: the reference is one performance core of the director's M5 Max, on bare metal; the headline is "2,000 Normal grains per instance at 48 kHz in X% of one Apple M5 Max performance core", X measured on the real engine's render path at the end of M1 (sub-project 6) and published with its commit SHA and block sizes, with no ceiling; CI's floor legs keep running the full benchmark, as informative figures; CI's CPU gate stays a regression gate on in-process ratios (carry-over B-I2); sub-project 2 tries four levers (prefetch, an interleaved source, cache-aware scheduling, contiguous loads with shuffles), keeps each that wins on the M5 Max or any CI leg without regressing another, then stops. The director signed off the Part I wordings: §5 "with published figures (§16)", and §10 criterion 3 "The grain-count and CPU figures published in §16 are measured on the reference hardware and reproducible." Supersedes #23. See `docs/superpowers/specs/2026-10-27-cpu-headline-design.md`. Decided 2026-10-27 by the director, during M1; see §5, §10, §16, §19, §20 and §21 |
| 40 | Freeze is live until toggled | Final-review finding I1 of M1 sub-project 1: Freeze is saved in the state but the live ring isn't (persistence is an M2 non-goal), so a Freeze that was on when a fresh ring started froze that ring's first 20 ms, usually silence, and the effect stayed silent until the user toggled Freeze. That happened on every project reopened with Freeze on, every reactivation and every CLAP `reset()`, which clap-wrapper's VST3 calls on each `setProcessing(false)`. The director chose "live until toggled" over documenting the silence: the `Processor` engages Freeze only on an off→on transition after an activation or reset, evaluated on the control grid; until then the effect plays live grains while Freeze reads On. The full arming rule (ruling M1-ac, kept by the director in sub-project 1's final fix re-review, 2026-10-27): Freeze arms when it is seen off at a call's first sample, at an applied grid boundary or in a `setBase` of Freeze (params.flush or a state load); an activation and a reset disarm it, and the reset itself doesn't arm, so a Freeze turned on after a VST3 suspend, before audio flows, stays live until toggled; modulation that takes the effective Freeze off and back on counts as a toggle. In the FX a note-on freezes without arming (§23 #48). Once engaged, the 20 ms ramp and the admission clamp (M1-p) are unchanged. With it the effect is always audible after a reload; the cost is that a reopened frozen pad is not held until M2 persists the ring and revisits this. No golden starts frozen, so none changed. Decided 2026-10-27 by the director, during M1 sub-project 1 (final review); see §14 |
| 41 | §20's M2 exit wording after #39 | §20's M2 row still said "grain target met", but #39 removed the CPU target: the §16 headline is measured at the end of M1 and published, with no ceiling, and CI's CPU gate is a regression gate on in-process ratios. The director's wording replaces it: "no CPU regression vs the M1 measured headline". Decided 2026-10-27 by the director, during M1 sub-project 1; see §20 and #39 |
| 42 | Reference templates named by their upstream repositories | Part II and CLAUDE.md named the three reference templates (shortcircuit-xt, surge, OB-Xf) only by local paths, which a reader without those clones can't follow. Each mention of a template now names its GitHub repository under the surge-synthesizer organization, checked against each local clone's `origin`: the "Key sources" list gives one bullet per template, with its local clone next to its URL; §15's CLAP-first paragraph links shortcircuit-xt once, with its template files' paths relative to the repository; CLAUDE.md's "Reference templates" bullet gives the URLs and says a session may read the local clones when they're present. A correction; no decision changes. Decided 2026-10-27 by the director, during M1 sub-project 1; see §15 and "Key sources" below |
| 43 | Sub-project 2's kernel bits and the Eco tier | Rulings 1 and 2 of M1 sub-project 2. **Kernel bits:** the renderer gets new per-sample math, defined once by a scalar reference: the window phase is `e × invDuration`, with `invDuration` latched once, and each channel's gain is one latched product, so a sample adds `read × (w × gain_c)` (on the live ring, `read × ((w × f) × gain_c)`). The 4-wide SIMD kernels match that reference bit for bit on every platform's own build, with no horizontal operations and no FMA. The audio-metric goldens are regenerated once, in one commit that says why; the grain-event logs don't change. **Eco is engine-only until M2:** the tier is latched per grain, at its onset, from `CloudParams::quality`, and stored in the pool; `gg-render`'s sample mode takes an optional `stream.quality`, and a golden case covers Eco. Both plugins stay Normal; a Quality parameter arrives in M2 with HQ and the budget manager. Decided 2026-10-27 by the director, during M1 sub-project 2; see §14 and `docs/superpowers/specs/2026-10-27-m1-cloud-engine-design.md` §2–§3 |
| 44 | The CPU gate is report-only through M1; X is the Sample-mode case, the live figure beside it | Rulings 3 and 4 of M1 sub-project 2. **The gate:** sub-project 2 builds CI's CPU gate (in-process ratios against a frozen reference kernel, per-leg baselines and a verdict), and CI reports the verdict without failing; sub-project 6 makes it blocking, with the published X. **The headline's source:** X is the Sample-mode case, 2,000 Normal grains of 100 ms reading a stereo buffer in RAM; `gg-bench` also times the same case over the live ring, and that figure is published beside X as the FX's. Decided 2026-10-27 by the director, during M1 sub-project 2; see §16, §19, §20 and `docs/superpowers/specs/2026-10-27-m1-cloud-engine-design.md` §2 and §5 |
| 45 | The Cloud density ceiling is the sample rate | Ruling 5 of M1 sub-project 2, which amends ruling M1-a as recorded in #36. The §16 case needs 2,000 overlapping grains of 100 ms, which is 20 kHz at 48 kHz, but the scheduler clamped a positive density to [0.01, min(4000, sampleRate)]. The 4 kHz cap goes: a positive density clamps to [0.01, sampleRate], so onsets average at most one per output sample, as in M0. The plugins' Density parameter still stops at 400 Hz, so nothing a user can reach changes, and no golden changes. §14's clock range, "0.01 Hz to about 4 kHz", becomes "0.01 Hz up to the sample rate". Decided 2026-10-27 by the director, during M1 sub-project 2; see §14 and `docs/superpowers/specs/2026-10-27-m1-cloud-engine-design.md` §2 |
| 46 | §16 levers tried: the interleaved source kept, three rejected | Sub-project 2 tried §16's four levers, each judged by in-process ratios against the kernel before it, on the M5 Max and on CI's three legs. **Kept: the stereo-interleaved source,** both halves. A stereo sample source gets an interleaved copy (L, R per frame), so each SIMD tap is one address; the live ring stores L, R per slot. On the M5 Max the Sample case is 7–9% faster and the live case 9–13%; the live half also wins 8–14% on Windows and loses nowhere. **The director's exception:** on the Windows leg the interleaved *sample* half regressed the sample ratio by 1–2%, and Eco by up to 7% at block 32, which §16's keep rule ("no leg regresses beyond its spread") would reject. Shown the M5's 7–9% sample win against Windows' 1–2% sample regression, the director decided to keep it ("I'd rather keep it"), at 2× memory for a stereo source. **Rejected:** per-grain prefetch (its sample half regressed Eco on the x86 legs; its live half, ported onto the interleaved ring, showed no win on the M5, and its 5–6% live win on Linux, with both runs on the same CPU model, was within that leg's 6% drift between runs for unchanged code, and the M1 VM's lone live wins at block 32 (0.867, then 0.667 ported) were inside that leg's larger drift: in the port's run its unchanged sample path moved to 0.821 at block 32); cache-aware scheduling (11% to 77% slower on every leg); contiguous loads with shuffles (5% to 49% slower on x86; ported to arm64 only, it no longer paid on any path the engine runs, since the interleaved reads are already contiguous). None of the rejected levers is merged; their branches stay for the record. Optimization stops here; sub-project 6 measures X. Decided 2026-10-27 by the director, during M1 sub-project 2; see §16 and `docs/spikes/m1-cpu-levers.md` |
| 47 | CI CPU baselines are per CPU model, from separate runs | Found while judging #46's levers: on CI, ratios for unchanged code drifted between runs far beyond the in-run IQR, up to ~20% on Windows, where the two runs landed on different CPU models (GitHub's hosted x86 pools mix AMD EPYC 7763, 9V45 and 9V74 and Intel Xeon 8370C: Linux has run on all four, Windows on the three EPYCs), and up to 6% on Linux on the same model. One run's IQR therefore says nothing about the next run. The controller's ruling: **baselines are per CPU model.** A leg's baseline file holds one entry per `machine`; `gg-bench --make-baseline` groups its input runs by `machine`, and each entry's tolerance is `max(0.10, 2 × (max − min) / median)` over separate CI runs. `--baseline` gives a verdict only against the entry for the run's own machine; on a model with no entry it prints `no baseline for <machine>; no verdict` and exits 0. A model not yet seen gets its entry from later runs on it. The first baselines have entries for the mac leg's Apple M1 VM, Windows' EPYC 7763 and 9V74, and Linux's EPYC 7763 and 9V45. This supports §19's report-only gate through M1; sub-project 6 makes the gate blocking with the published X and adds the models the first baselines lack (amended by #60, #61, #62: the gate turns blocking at 1.0.0). Recorded 2026-10-27, during M1 sub-project 2; see §16, §19 and `docs/spikes/m1-cpu-levers.md` ("CI variance") |
| 48 | Sub-project 3 (Sources): notes in both products, the instrument's sample, the loader, the chunked state and the search for a missing sample | The director's eight decisions, by forced choice: **D1** loading is silent and notes start the instrument's sound, the Cloud stream reading the sample repitched to the held notes; **D2** a chord is one cloud, each grain picking one held note at random from the counter RNG, neither mono nor M3's polyphony pulled forward; **D3** a minimal loader in today's editor (Load…, a drop target, the file name and status, Relink…, Sample folder…) until sub-project 5's canvas; **D4** in the FX a note holds and plays: note-on freezes the live ring and repitches the grains to the held notes, and releasing every note unfreezes it, with no capture buffer; **D5** a sample missing on reopen is searched for, then shown missing with relink, never embedded; **D6** the details as proposed: note 60 plays the sample at its own pitch, velocity does nothing, the Freeze parameter or any held note freezes the FX, a note-on engages even right after a reset, WAV, AIFF, FLAC and MP3, conversion to the host rate with r8brain, a 10-minute cap, the instrument's Position as 0–100% of the file with Spray in seconds, a new sample swapping in while old grains finish on the old one, and `gg-render`'s instrument mode, notes and file input; **D7** the state becomes §18's chunked container now (`GGST`, with `PTCH` and `MEDI`), so the format changes once before release; **D8** a session restore loads its sample before it returns, so every render after opening includes it, at the cost of a few seconds' longer open; one `process()` call applies the restored values and sample together, except while two sources are still in play (a swap draining), when that call applies the values and the sample follows within Size (§18). **Rulings made during the build:** decoding memory follows the file's content, never its header alone (R7); truncated files are refused (R8), but an uncompressed WAV or AIFF may declare up to 1 KiB of audio beyond its end, 1024 bytes included (R10, R12), an MP3 may fall two MP3 frames short and a FLAC none; `encodeState` never writes a `MEDI` entry that wouldn't decode, leaving it out in release builds and asserting in debug (R9); MIDI CC 123 releases only its own channel, as MIDI 1.0 says, and an all-off on channel −1 releases everything (R14); a CLAP note-off or choke addressed by `note_id` alone (channel and key −1) resolves through a fixed 256-entry ID table on the audio thread, and an unknown ID releases every note (R15); the settings file is read and written on the main thread only, through a temporary file unique to each save (R16); a note whose Off would round to its On's frame is bad input to `gg-render` (R17). **Ruling R18 is Claude's, made during the build; the director confirmed it on 2026-10-28.** It amends the spec's §6.4, which had the loader convert again in the background when the instrument is activated at a new rate: a reactivation at a new sample rate now decodes and converts synchronously in `activate()`, because the background swap made output depend on the worker's speed, against "no behavior that adapts to measured CPU" (CLAUDE.md) and D8's identical exports. If the file has gone or changed, the old source keeps playing at the right pitch and the status reads Missing or Changed. The cost: a reactivation at a new rate with a long sample blocks the main thread for seconds, as a restore does. **Also recorded here:** the 10-minute cap is 600 s of the file's own audio, and a state is read up to 64 MiB (carry-over T7 218); ruling M1-ac's full Freeze arming rule, and that a note-on engages without arming, now stated in §14 and #40 (carry-over M1-ac); §18's Position, now per product; `gg-media`, the new JUCE-free library, in §15; §21's "async decode" becomes background decoding for interactive loads; §20's M1 row notes that note-triggered freeze is built as hold and play. See `docs/superpowers/specs/2026-10-28-m1-sources-design.md` §4 and §6. Decided 2026-10-28 by the director, during M1 sub-project 3; the rulings made during its build; see §14, §15, §18, §20 and §21 |
| 49 | Sub-project 3's final review: hardening, and ruling R19 | Claude's, in the final whole-branch review of M1 sub-project 3; the fixes are code and tests, and change no golden. **Ruling R19, confirmed by the director with R18 on 2026-10-28:** a session restore whose sample entry has the current entry's fingerprint (SHA-256 and size), while that sample is in memory, keeps it: no search, no decode, no hash, and the current entry stays. Active, only the values go to the audio thread, through D8's hand-off, in an offer that keeps the source (`SourceExchange::offerValues`); a keep offer that replaces an untaken one takes over its source, so a load not yet taken still arrives. Hosts restore state for undo, A/B compare and preset recall, which had re-decoded for seconds and silenced a sample still in memory whose file had gone; the cost is that an instance that kept its audio differs from a fresh open only if the file vanished mid-session. **Hardening:** gg-media opens WAV, AIFF, RF64 and W64 itself and gives dr_wav a seek that refuses any position outside the file, since a wrapped or huge chunk size (an RF64 `ds64` under 24 bytes, a W64 junk chunk of 2^63) had dr_wav seek forward in 2^31-byte steps for about 47 minutes, freezing the loader, `deactivate()` and `gg-render`; r8brain's MaxInLen is `clamp(65536 × from / to, 1, 4096)` per conversion, since a 1 Hz file converted to 192 kHz had peaked at 7 GB, and to 768 kHz overflowed r8brain's int lengths; a converted buffer keeps at least one frame of a file that has any; XML nested more than 64 deep is rejected before tinyxml parses it (§18), since a crafted project overflowed the stack and crashed the host as it opened; `MEDI` rejects a negative frame count; a WAV whose header declares more than it holds is "truncated" before it is "too long", so an unfinalized recording says so. **Also recorded:** a newer `MEDI` version fails the whole state, deliberately, until the format freezes (§18); for up to one block a fourth source can exist (§14). Decided 2026-10-28 by Claude, during M1 sub-project 3 (final review); see §14 and §18 |
| 50 | Sub-project 4 (Space): a per-layer diffuser and a patched Reverb 2, on the grid with a 32-sample return, an idle rule, an infinite Decay and a constant tail; and the carry-over policy | The director's eight decisions, by forced choice: **S1** Space is both a diffuser that belongs to the grain sound and a separate reverb with its own controls; **S2** the FX's reverb always hears the grains, plus a separate Dry to Reverb send for the input; **S3** the reverb's controls are Amount, Decay, Size, Damping, Pre-delay and Modulation; **S4** a fresh instance is dry (Diffusion, Amount and Dry to Reverb at 0), and presets choose their own space; **S5** Decay's top is infinite, the input still added; **S6** the reverb is Surge's Reverb 2 from sst-effects, with an in-house diffuser; **S7** at ∞ the LF damping is switched off and the highs still fade by Damping, in a small pinned patch; **S8** Pre-delay is smoothed in the same patch, and Size's artifact when it moves is accepted and logged. **The design** (Claude's calls in the approved spec): the diffuser is per layer, so §14's global chain becomes delay · reverb · limiter; Level and the soft limiter stay before the reverb, which hears the wet signal as the listener does (scaled by Mix in the FX) and whose return passes a soft limiter of its own before the sum, so every sample is finite and |out| ≤ 2 + |d|; the return is added only while the reverb is active, so with every Space parameter at its default both products render bit-identically to before, the sign of zero included, and every earlier golden's WAV keeps its SHA-256; Reverb 2 runs on the 32-sample control grid, one chunk per cell, so its return is a constant 32 samples late at any block size and those samples count toward Pre-delay (its real minimum), with no latency reported; the idle rule: a chunk whose first sample has the Amount ramp at exactly 0 doesn't run the reverb, so raising Amount starts from an empty room; eight parameters appended after Freeze at the next free IDs (`0x01000B`, `0x000003`–`0x000009`), Decay's top 2% shown "∞", Dry to Reverb hidden in the instrument, no patch version bump; a second remote-controls page, "Space"; the CLAP `tail` extension; `gg-bench --space`, report-only. **Rulings made during the build:** P1, Pre-delay glides at no more than 0.5 samples per sample (a fixed 0.1 s glide over 500 ms bent the pitch fivefold), so at most an octave down or a fifth up; P2, at ∞ with Damping up the HF one-pole attenuates the mids on every pass too, so only the lows hold (the listening check judges it); R-T2a, a pre-delay set while nothing has run since a clear applies at once; R-T2b, a clear zeroes only what a later read can reach (about 0.5 MB at 48 kHz, not 14.5 MB), with `clearStep` to spread it; R-T5a, idle chunks spread that clear, 16,384 floats (64 KiB) each, and the first active chunk finishes the rest; R-T5b, `reset()` clears all at once, as hosts expect a reset to cost, and a new rate builds a new reverb; R-T5c, Reverb 2's own decay formula is kept, though the measured T60 at Damping 0 is 1.72, 2.89 and 5.09 s for Decay labels 1, 2 and 4 s (a voicing choice, for the listening check); R-T7a, the tail is a constant per product and never announced: the FX's infinite, the instrument's 122.5 s (Size's 2 s + Pre-delay's 0.5 s + 2 × Decay's 60 s), because clap-wrapper turns `tail.changed` into VST3's `restartComponent(kLatencyChanged)`, a host deactivate and reactivate that would empty the FX's ring and drop its notes and Freeze at every note, Freeze toggle or Amount change, which supersedes the spec's tail that followed the values (the cost: the FX never sleeps on silence, and an instrument at ∞ is suspended 122.5 s after its last note in hosts that suspend silent plugins); R-T8a, at ∞ a 0.5 Hz DC blocker in the loop replaces the bypassed LF damping, since the lossless loop integrated the grains' DC (Task 8's `inst-infinite` golden saw it grow to −0.02); R-T8b (the fix wave after the final review), the switch is continuous: at ∞ the LF dampers themselves run at the DC blocker's coefficient, keeping their state, and entering or leaving ∞ glides over 30 ms in whole blocks (the decay multiplier linearly toward 1, the LF coefficient geometrically, the HF clamp's floor linearly, `loopClip` while any share of ∞ remains), at once before a reverb's first block so one that starts at ∞ is unchanged; before it, entering swapped the LF dampers for a blocker at rest, stepping the loop's lows (a held −32 dBFS pad: +26 dB above 3 kHz over a render that never switched), and leaving dropped the decay multiplier from 1 within one block (+8 dB), and now both stay at least 3.7 dB below. Reverb Decay's typed text takes a bare number as seconds, "ms" for milliseconds (the final review, 2026-10-28). **∞ is bounded by `loopClip`** (`x` up to |x| = 1, else `sign(x) × (2 − 1/|x|)`, below 2, no libm), applied to each block's loop signal after the dampers. **The patch** (§15): a copy of `Reverb2.h` at `987b39d` with every change marked `// GG PATCH`: ∞ (the decay multiplier exactly 1, the LF dampers at the DC blocker's coefficient, the HF clamp at 0, `loopClip`, the 30 ms glide), the smoothed pre-delay, and `clear`/`clearStep`; unused, it renders bit-identically to upstream; sst-effects `987b39d` and sst-filters `87a171b` are pinned with shortcircuit-xt's pins. **Measured:** Reverb 2 is 14,533,632 bytes per instance; Space costs 28.49 ns per stereo sample (min 28.30, IQR 0.09), 0.137% of one M5 Max core at 48 kHz (`gg-bench --space`, release, timed by the fix wave at `ac81dfb`, its final code; the bench and the timed code are unchanged since; load average about 2.7, ordinary desktop activity; sub-project 6 makes the published idle-machine measurement); an hour of full-scale sine or noise at ∞ peaks at 1.685 and 1.718; the first audible return is about 60 ms after the input at Pre-delay 0 and Size 50%, Reverb 2's own taps; moving Size mid-sound over a sine scores 31 to 228 on the golden click detector (limit 30), so `fx-space-automate` moves Size once mid-sound and once in the tail; a Pre-delay glide leaves a broadband haze near −90 dBFS; clap-wrapper's AUv2 hard-codes `SupportsTail()` false, so AU hosts never see the tail. Reverb 2's damping doesn't scale with the sample rate, Surge's behavior, kept. **The carry-over policy** (the director, 2026-10-28; CLAUDE.md, "Working style"; retired by #67, 2026-10-30, but for its no-upstream-filing rule, which stands): fix, don't carry, every minor a reviewer raises unless it is blocked outside our control or genuinely large; a sub-project that edits a file closes every open "when touched" row for it; no net growth of open rows over a sub-project; sub-project 6 burns down every open row, M0's included; no external reports or upstream filings, so §15's planned clap-wrapper contributions become wants that wait for upstream, and the Reverb 2 patch isn't offered upstream. The spec's §6.3, §6.4 and §6.7 are amended for P1, P2, R-T2a, R-T2b, R-T5a, R-T5b, R-T7a, R-T8a, R-T8b, `loopClip` and, in §6.3's fork, the policy. See `docs/superpowers/specs/2026-10-28-m1-space-design.md` §4 and §6. Decided 2026-10-28 by the director (S1–S8 and the policy), during M1 sub-project 4; the rulings are Claude's, made during its build; see §14, §15, §16, §18 and §20 |
| 51 | Two open questions added: C++23, and wider x86 SIMD | Two rows added to §22: moving from C++20 to C++23 (for `std::expected` and smaller conveniences, against gcc-12's partial library support and Apple's libc++ gating some C++23 library features by macOS version), resolved in M2 with a toolchain floor raise; and AVX2 (8-wide) grain kernels on x86, which would need kernel paths chosen at load by CPU features with identical output, a ruling on §14's no-CPU-adaptive-behavior rule and rebuilt x86 baselines, resolved by a timeboxed spike if sub-project 6's measured x86 figure misses §16's target, or for M2's four layers. Neither changes anything yet. Decided 2026-10-28 by the director, during M1 sub-project 4; see §22 |
| 52 | §19 says what is bit-exact; no golden pins the audio's bits | §19 put "Bit-exact comparison applies on the same platform" under golden renders compared by metrics, where nothing compared bits, so a 5% gain bug passed every golden (review finding B-m7, carry-over rows 68 and 103). The event line has since carried each grain's gain and pan (M1 sub-project 1), which pins the grains' level and placement on every platform, but not the output's: the Processor's Level and Mix ramps, the soft limiter, the diffuser and the reverb come after the grains, outside the log, and only the metrics compare them, within 0.5 dB on the RMS envelope and the peak, so a gain error there under about 6% (B-m7's 5% is 0.42 dB) still passes every golden on every platform. What remained was a choice between a per-platform digest of each golden's output and saying plainly what is bit-exact. **The wording, not the digest:** a digest would need a copy per CI platform (macOS arm64, its x86_64 slice under Rosetta, Windows and Linux), refreshed from CI artifacts after every DSP change, and it would fail when a runner's compiler or C library changed, with no change of ours. What it would add is catching a change in the samples, after the grains or in them, that the metrics' tolerances miss. For a change declared output-neutral, a before-and-after SHA-256 of every golden's WAV on one machine catches that at the moment of change, which is when it matters; Space did it by hand (#50), and `scripts/update-goldens.sh` now prints each case's hash and lists the cases whose WAV changed since its previous run in that build tree (a 5% gain on the two Space cases' output, tried and reverted, passed all 23 golden tests and was listed). A change meant to alter the output still relies on the metrics, the spectrograms and the listening gate. So §19 now lists the bit-exact checks: the event log on every platform, the determinism tests on each (now naming the repeated render they already include), the SIMD kernels against their scalar references, and the WAV check for a change meant to leave the output alone, and says that Level, Mix, the limiter and Space are compared by metrics alone. §10's "bit-exact on the same platform" is unchanged; the determinism tests verify it. Decided 2026-10-28 by Claude, during the M1 carry-over burn-down; accepted by the director, 2026-10-29; see §19 |
| 53 | A newer `MEDI` loads with no sample and saves back unchanged; the XML pre-scan follows tinyxml | **The director's decision, by forced choice, on 2026-10-28** (the carry-over row that #49 left open, "A newer `MEDI` version fails the whole state"): a state whose `MEDI` version is newer than the build's loads its patch, keeps the unknown manifest's bytes verbatim so a re-save writes them back unchanged, and reports the status *sample from a newer version*, with no sample loaded. It replaces #49's rejection of the whole state, which kept an older build from dropping a sample reference it couldn't read by refusing the session outright, values and all. Keeping the bytes handles the save-over risk instead: saving the session doesn't lose the reference, and only loading or relinking a sample replaces it, the user's own choice. The manifest must still parse (well-formed, its `<media>` root alone, a whole decimal `version`), so a corrupt one still fails the state; nothing inside a newer one is read. The FX, which has no sample, still writes an empty `MEDI`. **Also recorded (Claude's, in the same burn-down task):** the 64-deep XML pre-scan (§18, #49) could be bypassed, so a crafted project still overflowed the stack. Four document shapes hid nesting from it: an unknown node (`<1 x="<a>…">`), a declaration's quoted values, a numeric character reference (which tinyxml ends at the first `;` and reads back to its `x`), and a UTF-8 lead byte that swallows the next bytes once a declaration or a byte-order mark turns on tinyxml's UTF-8 reading. The scan now follows tinyxml's `Identify`, counts a self-closed element as a level, and refuses what it can't read as tinyxml does: `<?` after the first node, a declaration that doesn't end at its first `>` under every case folding, an unquoted attribute value, a malformed `&#` reference and, in UTF-8 reading, a lead byte without its continuation bytes. A property test checks the scan against tinyxml on random documents. Decided 2026-10-28 by the director (the `MEDI` behavior) and Claude (the scan), during the M1 carry-over burn-down; see §18 |
| 54 | Ruling R7 amended: a compressed file's header is trusted up to 64× its size | Claude's, in the M1 carry-over burn-down (Task 7, carry-over row 187), **confirmed by the director, 2026-10-29**; controller ruling BD-T7a. R7 said decoding memory follows what the file holds, never only what its header claims, so FLAC, MP3 and ADPCM grew as they decoded and peaked at about twice the audio. Now a header whose frame count is at most 64 bytes of decoded audio per byte of the file (ordinary audio decodes to 2 to 22) is trusted: the audio decodes straight into one buffer of that count, which the `SampleBuffer` adopts, and peaks at about 1.01× the audio. The cost: a header that lies within the ratio costs up to 64× the file's size, allocated and zeroed, before the decode proves the file truncated; before, a liar cost about twice what had decoded. The absolute worst case for a file of a given size is unchanged, since an honest file of near-silence already decodes to far more. Beyond the ratio, or without a count, decoding grows in blocks as before, to about twice the audio that decoded. M2's per-instance memory cap remains the real bound on decoding. Decided 2026-10-29 by Claude, during the M1 carry-over burn-down; see §14 |
| 55 | utf8proc normalizes the names the search for a missing sample compares | Carry-over [190], in the M1 burn-down (Task 8). The search matched file names byte for byte, so a name saved in NFC wasn't found under its NFD spelling (macOS's HFS+ stores names decomposed; ext4 and NTFS keep whichever they're given) or the other way round. Comparing them needs Unicode's normalization tables. macOS has `CFStringNormalize` and Windows `NormalizeString`, but Linux has no system API (ICU isn't a given), and per-platform answers would follow each OS's Unicode version. So **utf8proc** (MIT; its Unicode data under the Unicode data license, permissive and AGPL-compatible), pinned at tag `v2.12.0` (`79cdd5a`, Unicode 18), gives every platform the same answer: one C file, built as a static library without our warning flags and linked privately into `gg-media`. The search tries the saved spelling, then its NFC and NFD spellings, at each step that opens a path, and the scan compares each listed name with the saved one in NFC: canonical equivalence only, with no case folding and no compatibility forms. A name that isn't valid UTF-8 matches only itself. Decided 2026-10-28 by Claude, during the M1 burn-down; see §15 |
| 56 | Windows standalone ASIO, from the ASIO SDK 2.3.4 under GPLv3 | The director decided to enable ASIO in the Windows standalone under the Steinberg ASIO SDK's GPLv3 option (carry-over row 133; the §22 question). RtAudio's own bundled ASIO files still have no license grant, so #30's rule stands: they are never compiled, and RtAudio's `include/` is never on a path. On Windows `libs/CMakeLists.txt` creates RtAudio itself, before clap-wrapper's standalone asks for it, with no API of its own (ASIO, WASAPI and DirectSound off), then adds RtAudio's ASIO backend built against sst-plugininfra's `libs/asiosdk` (ASIO SDK 2.3.4): exactly `common/asio.cpp` ("Steinberg ASIO License or GPLv3", used under GPLv3), `host/asiodrivers.cpp` and `host/pc/asiolist.cpp` (BSD-3-Clause-style), unmodified. Our own stand-in for RtAudio's `iasiothiscallresolver.h` (`libs/thirdparty-impl/rtaudio-asio/`) does two jobs: x64 ClangCL needs no thiscall resolver, so it has none; and it gives the SDK's `ASIOExit()` a non-null `asioDrivers` that never loads a driver, since RtAudio keeps its own and its copy of `asio.cpp` adds a null check the SDK's lacks. RtAudio's other changes to its copies aren't needed: their ANSI registry calls matter only under `UNICODE`, which RtAudio isn't compiled with (the SDK's `asiolist.cpp` doesn't compile with it), and the SDK's `delete` of a one-element array of a trivially destructible struct (theirs is `delete[]`) is a formal mismatch, harmless under the MSVC ABI. The configure fails if any RtAudio source naming ASIO isn't one of those three, if RtAudio's `include/` reaches its include path, or if the SDK's `LICENSE.txt` stops offering GPLv3. The standalone's WASAPI audio and WinMM MIDI stay clap-wrapper's own; ASIO is offered beside them, with the driver's control panel. macOS and Linux are unchanged. The GPLv3-only SDK files join a binary that is already AGPLv3 and GPLv3 (§12). The configure also pins the SHA-256 (line endings normalized) of the SDK's `LICENSE.txt` and of every SDK file compiled or included, so a pin bump that changes one fails until its license is reviewed again; refuses `CLAP_WRAPPER_STANDALONE_RTAUDIO` on Windows (it would drop the native WASAPI); forces RtAudio's JACK and PulseAudio off there; and fails unless both standalones' clap-wrapper libraries define `CLAP_WRAPPER_STANDALONE_ASIO=1` and link RtAudio, so a lost handoff can't build silently without ASIO. **Distribution:** a distributed Windows build carries the SDK's `LICENSE.txt` and the GPLv3 text, the `host/` files' BSD notice, and the line "ASIO is a registered trademark of Steinberg Media Technologies GmbH." in its credits and notices; `THIRD_PARTY_NOTICES.md` holds them. **Trademark** (the director, 2026-10-29: attribution, no logo): Steinberg's ASIO Usage Guidelines in the SDK ask for the ASIO-compatible logo in a configuring dialog where ASIO is enabled manually, and for ® on first use with that attribution line in credits and documentation. We give the attribution (® on first use in our documents, the line in `THIRD_PARTY_NOTICES.md`) and don't use the logo: the plain word "ASIO" that clap-wrapper's settings dialog shows (the API's name and an "ASIO Control Panel..." button) is treated as a descriptive reference to the driver type, and the dialog is clap-wrapper's. Decided 2026-10-28 by the director, during the M1 carry-over burn-down (the trademark ruling 2026-10-29); see §15 and §22 |
| 57 | CI speed-ups, a cache for the SDKs, and the click score's accepted blind spot | **The director's decisions of 2026-10-29,** made during the M1 carry-over burn-down's CI rounds, plus the review's amendments. **CI:** (1) a pull request runs `gg-bench --quick` as a smoke check (no baseline, no verdict, no artifact); the full-length run (§16, §19) happens on pushes to `main`, the nightly schedule and `workflow_dispatch`, and the blocking gate that sub-project 6 adds (amended by #60, #61, #62: blocking at 1.0.0) must choose its pull-request run length, since a quick run's ratios are too noisy for a baseline; (2) ccache on the macOS and Linux legs and the sanitizer job (Windows' Visual Studio generator has no compiler launcher; reversed by #76), CI only, so no local preset changes; shallow submodules; the pinned LLVM archive cached by version and SHA-256 (the SHA-256 is checked on a hit too); (3) all caches are restored by every run and saved only on `main` (a push, the nightly, a dispatch), also after a failed build, because GitHub scopes a cache to the ref that saved it and a pull request's is never read; (4) concurrency by pull-request number, or by run id for every other run, so a newer push cancels a pull request's run and nothing else is canceled; (5) the SDKs that clap-wrapper downloads (VST3 SDK, AudioUnitSDK, RtAudio, RtMidi, wil) are fetched by `scripts/fetch-sdks.sh` at the tags clap-wrapper pins, cached, and passed to Configure as `-DVST3_SDK_ROOT` and its siblings, because CPM's own cache is skipped when a package has a `SOURCE_DIR` (carry-over row 96). None of this changes what is built. **The click score (carry-over row 58):** `metricsVersion` 2 normalizes the click score by the level within ±5 ms and lowers the limit from 30 to 15. It catches 220 of 304 injected -20 dBFS steps; the two dense-noise cases catch none, and none of the same positions' 0 dBFS spikes (scores 9.3 to 14.6 against 15), because their own noise hides them. The director chose to **accept that blind spot** (option A) rather than lower the limit to 10 and exempt `fx-space-automate`'s Size click (11.9) or move that automation. §19's "click detection" says so; `tests/metrics/click_injection_tests.cpp` pins each case's count so a change that moves one is a decision. Decided 2026-10-29 by the director; see §16, §19 |
| 58 | Sub-project 5 (Perform UI): eight relative macros, Texture, Feedback into the live ring, the Perform view on sst-jucegui and the audio→UI path | The director's six decisions (2026-10-27, in brainstorming): **(1)** macros are relative, each a modulation source offsetting its targets from the preset's base values, one writer per parameter; **(2)** Texture sweeps the grain envelope from smooth to gated over the lower three quarters of its macro's travel and adds Space's diffusion in the top quarter; **(3)** Feedback regenerates the wet output into the FX's live ring, and the instrument, with no writable buffer until M2's resample source, has Stereo in its seventh macro slot (the spec's "eighth" corrected, below); **(4)** the eight recipes, every depth tuned by ear at the listening check; **(5)** the canvas: the FX lane's write head fixed at the right edge with history scrolling left, the canvas on top and one macro row below in remote-control order, and a canvas drag editing base values, with ghost windows where modulation moves the grains; **(6)** pan on the vertical axis, sst-jucegui's controls on the existing JUCE 9 shim, §17's audio→UI path, a fresh seed per instance (#59), and Edit, Mod, Mutate, the preset browser and lanes B–D left in M2. **The design** (the spec and the plan, as built): ten parameters appended after Dry to Reverb at the IDs the spec reserved, Texture `0x010009`, Feedback `0x01000A` (0–0.95, hidden in the instrument) and Macros 1–8 `0x000011`–`0x000018` (Macro Freeze stepped; Macros 7 and 8 named per product, Macro 7 resting at 0 in the FX and 0.5 in the instrument), 28 in all, with no patch version bump (§18, `6af2583`); a "Perform" remote-controls page of the macros first, "Cloud" and "Space" after it, `page_id` = position; the effective value `clamp(base + mod + Σ curves)` in one clamp, each macro's own value `clamp(base + mod)`, the fixed factory recipes of §14 in each target's CLAP value domain, plain float with no libm, every offset exactly +0.0f at rest (bipolar 0.5, unipolar 0) so a resting preset is bit-identical, on the 32-sample grid (`962e153`); Texture's fixed family (`aMin = min(0.5, max(0.05, 16/d))`; τ ≤ 0.5: `a = 0.5 + 0.4τ`, `r = 0.5 − 0.4τ`; τ > 0.5: the lerp from (0.7, 0.3) to (aMin, aMin), sustain between), the window `hann(warp(φ))` with one `warpPhase` for the scalar references and the SIMD interior, the identity bit for bit at Texture 0, four doubles latched per grain, and `"tex"` in the grain-event line, which rewrote every `.events.jsonl` once and moved no WAV (`f54cb85`); Feedback's ring input `finite(in[t]) + fb(t)·limited[t − 256]`, `limited` after Level and the soft limiter, before the mix, `fb` a 10 ms ramp like Mix's, `fb ≤ 0.95`, D = 256 = M (the ring's head margin, static-asserted), the FX rendered in pieces of at most 256 samples so the output is bit-identical at any block size and maxBlockSize, a per-sample guard that writes the input alone wherever `fb` is 0, Freeze winning because the frozen ring's write gain is 0 (`711f5fe`); the publish structures in gg-core, `SpscQueue<T, N>` (the grain queue of 4096, dropping the newest and counting them), `UiSnapshot` in a triple buffer and `LivePeaks` (`4061e72`), filled by the `Processor` from its own grain sink, with the atomic clock stored after each snapshot (`d406a91`); the UI→host path, a 1,024-entry `SpscQueue<UiParamChange>` drained by `process()` and `params.flush` before the host's events, every change sent on as CLAP gesture and value events by `try_push` after `request_flush` (`b1cbe6e`); `LaneModel` and `SplatRenderer::addCapsule` (`796f29e`), the lane clock and live-grain list (`f822e30`); the Perform view (§17): header, `GrainCanvas`, the macro row on sst-jucegui with its dark style sheet, 900 × 500 down to 640 × 360, and the instrument's loader strip with carry-over rows 198 and 199 closed (`d179a98`, `fe4de01`); three goldens, `fx-macros`, `fx-feedback` and `inst-texture` (§19), and the cross-feature block-size-invariance and allocation tests (`bac40b4`, `7ab5880`, `3d614bd`). With Texture 0, Feedback 0 and every macro at rest, every earlier golden's WAV kept its SHA-256 through every task. **Rulings made during the build:** PU-T8a, the FX's scan window is one-sided, `[Position, Position + Spray]` behind the head, as the engine draws a live grain (`position + spray × u`), its newer edge dragging as the body; the instrument's stays `Position ± Spray` (`b18b185`); PU-T3a, Texture's τ > 0.5 branch is the lerp `(1 − s)·0.7 + s·aMin`, the plan's curve, which lands exactly on its anchors where the plan's `0.7 + (aMin − 0.7)s` gives 0.050000000000000044 for 0.05; PU-T2a, a `setBase` of a macro jumps the ramps of every target it moves (Mix, Level, Diffusion, Feedback) and Macro Freeze is seen as Freeze for arming, so a loaded state never ramps in from stale values; PU-T4a, the freeze test checks the frozen ring bit for bit under Feedback 0.9 and the output's level with Feedback raised at the freeze, not the plan's 0.5 dB comparison, since regeneration before the freeze legitimately sits in the frozen buffer; PU-T7a, sst-jucegui's JUCE requirements are re-pointed at the headers-only `gg-juce-headers` plus `$<LINK_ONLY:clap_juce_shim>`, so JUCE compiles once, in the shim, and the configure fails if the wiring it replaces changes (`038b53a`, `6acc5fa`, `01f979a`); PU-T9a, the live peaks are indexed by ring time, the frames the head advanced over, not the sample clock, which a freeze makes diverge; PU-T9b, the FX writes the peak strip on every render piece, publishing or not, so an editor opening late never shows an older pass's peaks, while the grain queue and the snapshot stay gated on the editor (`c193784`); PU-T10a, the plugin owns one editor context for its life and updates its publish pointers in place on activate and deactivate, null while inactive, since the `Processor` they point into dies with deactivate; PU-T12a, `fx-macros` runs over a 330 Hz sine, not noise, which hid all sixteen injected clicks (a blind spot the director hadn't accepted), and `fx-feedback`'s input lasts 4 s, so Feedback 0.9 at 3 s has material (`3d614bd`); PU-T5a, the instrument's seed (in the loaded-state snapshot) and values (with its sample) may reach the audio thread in different blocks, accepted since a live load's timing is inexact anyway and the saved state is consistent (#59). PU-T13a (sub-project 5's documentation review), the 16-sample floor binds only Texture's gated anchor, `aMin = min(0.5, max(0.05, 16/d))`: for τ ≤ 0.5 the shape is Hann-like and its attack and release scale with the grain, as the default Hann always has (an 8-sample attack on a 16-sample grain at Texture 0 is M0's behavior, kept bit for bit), which amends the spec's §4 "never shorter than the 16-sample minimum grain"; PU-T11a, sst-clap-helpers' shim keeps the editor of a GUI a host destroys without ever showing it (no `set_parent`), and the next `guiCreate` trips its debug-only `jassert(!editor)`; publishing is correct either way (`EditorPlugin::guiDestroy` turns it off), and the fault waits on upstream in the carry-over. **The spec's corrections:** sst-jucegui is MIT (its `LICENSE.md`; §15 already said so), not GPL-3.0-or-later; the Feedback chunking is the `Processor`'s, not the engine's, since the `Processor` owns the render pieces; decision 3's Stereo replaces the instrument's seventh macro slot (Macro 7), not its eighth, which is Level. **Also recorded:** the peak pyramid is one 64-frame level in M1 (§17); a −0 base plus a −0 modulation now gives +0 once the resting offset is added (host-only input; no golden moves); the editor's 1,024-entry queue drops a change made while it is full, which takes a host that ignores `request_flush` (and doesn't process) for more than 1,024 changes, and a dropped gesture begin or end leaves the host an unbalanced gesture: an as-built exception to §15's lossless rule, accepted in Task 10's review and confirmed by the director 2026-10-29 (D1, below); in the 60 s Feedback 0.95 property test the wet RMS reads −0.18 dBFS over both seconds 16–26 and 50–60: it settles into self-oscillation just under the limiter, with no runaway (`711f5fe`; a listening-check item). **The director's decisions after the final review** (2026-10-29, by forced choice): **D1** the UI→audio parameter queue is accepted as built (1,024 entries, dropping a change only when a host ignores `request_flush` while more than that are queued), so §15's as-built exception stands, confirmed by the director 2026-10-29; **D2** auval lists the AU parameters in ascending CLAP-ID order, while §18 says the slot order is the AUv2 order: decided by the director 2026-10-29 to amend §18 to ID order in sub-project 6, by a row here, once clap-wrapper's AU `ParameterList` order is verified, with no code change (a carry-over row until then); **D3** the canvas frame budget is judged by the densest setting a host can reach staying at or under 3 ms in Live and Bitwig, plus an idle `gg-bench --canvas` run at 8,192 grains at or under 3 ms, taken with sub-project 6's X measurement (a run with nothing building, load average about 4, measured 2.80 ms, M0's 2.77 ms; the idle run is sub-project 6's), decided by the director 2026-10-29 (§16; the spec's §9); **D4** `fx-feedback`'s click-injection entry, 7 of the 16 steps caught, is accepted by the director 2026-10-29 (§19). The spec is amended in place for decision 3's slot (§1), PU-T2a (§3.1), the family, PU-T3a and PU-T13a (§4), the `Processor`'s pieces, the per-sample guard and PU-T4a (§5), PU-T8a (§6.2), PU-T9a, PU-T9b and PU-T10a (§6.4), MIT (§6.5), PU-T5a (§7), and PU-T12a and D3 (§9). See `docs/superpowers/specs/2026-10-27-m1-perform-ui-design.md` §1 and `docs/superpowers/plans/2026-10-29-m1-perform-ui.md`. Decided 2026-10-27 by the director (decisions, in brainstorming), 2026-10-29 by Claude (rulings) and 2026-10-29 by the director (D1–D4, after the final review), during M1 sub-project 5; see §14, §15, §16, §17, §18, §19 |
| 59 | A fresh seed per instance, and the seed hand-off | Sub-project 5's spec §7 (the director's decision 6, #58) and carry-over rows 115 and 123, closed by `5ac8a86`. **The fresh seed:** a new instance draws its engine seed on the main thread at construction from the OS's entropy source, two 32-bit draws from `std::random_device`, falling back to a splitmix64 of the steady clock if it throws, so two instances on two tracks make different clouds; the seed is saved in the state as before, so a reloaded session renders identically, and the audio thread's randomness still comes only from `gg::rng`, keyed by it. `gg-render`, the tests and the goldens keep explicit seeds; the plugin tests that compare instances load a state with seed 0 first. It replaces "new instances start at seed 0" (§18). **The hand-off (row 115):** a state loaded while active used to need the host to honour `request_restart` for its seed to take effect, and otherwise the engine kept the old seed while `stateSave` wrote the new one, so a reload sounded different from what was heard, latent while every instance used seed 0. Now the plugin's loaded-state snapshot carries the seed in both products, the audio thread calls `Processor::setSeed` (`Engine::setSeed`), which keys every grain scheduled from that `process()` call on (sounding grains keep their draws, and the grain index keeps counting), and `stateLoad` asks for no restart. In the instrument the values travel with the sample, so the seed and the values may land in different blocks (ruling PU-T5a). **The value mirror (row 123, ruling M1-x):** once the audio thread has applied a loaded state's values, it sets a flag and requests a callback, and `on_main_thread` asks the host to rescan the values (`CLAP_PARAM_RESCAN_VALUES`), since the first rescan, at the load, may have read a value a block in flight then overwrote. Decided 2026-10-27 by the director (the fresh seed, in the spec's brainstorming) and 2026-10-29 by Claude (the hand-off), during M1 sub-project 5; see §14 and §18 |
| 60 | M1 closed: X published as measured, the CPU gate blocking at 1.0, listening gate #1 passed, the AUv2 order is ID order | **The director's decisions of 2026-10-29,** made in sub-project 6's brainstorming, and the sub-project's measurements. **(1) The CPU gate stays report-only through M1 and turns blocking at 1.0,** the first public release, not in sub-project 6 as §19, §20 and #44 said; those sentences are amended, and carry-over rows 144 and 148 move to "At 1.0". **(2) Listening gate #1 is passed:** the director listened to the Sources, Space and Perform packs (`docs/listening/m1-sources`, `m1-space`, `m1-perform`) and accepted them; the macro depths stay at the spec's starting values (the Perform pack's tuning step closes as accepted as shipped); no further pack. **(3) The AUv2 parameter order is ascending parameter ID** (sub-project 5's decision D2, #58, carry-over row 233): verified in clap-wrapper `v0.17.0`'s `WrapAsAUV2::GetParameterList` (`libs/clap/clap-wrapper/src/wrapasauv2.cpp`), which, when a plugin provides no `CLAP_PLUGIN_AUV2_PARAM_ORDERING` extension (these plugins don't), falls back to `AUBase::GetParameterList`, and that lists the AudioUnitSDK's `flat_map` of parameters, a vector sorted by parameter ID (`AUScopeElement.h`); auval's listing agrees. §18's "slot order, and so the AUv2 order" is amended to say so, in both places and in the frozen list; the slot order itself is unchanged, and no code changes. **(4) X, measured and published** (§16): one idle session on the director's M5 Max, 2026-10-29, the `release` preset, at `be2291e` (main's engine at M1's close), the full default `gg-bench` run: `engine-sample` 2.44 / 2.21 / 2.03 ns per stereo grain-sample at blocks 32 / 64 / 256, **X = 19.5%** of one core at block 256 (23.4% / 21.2% at 32 / 64); the live figure 31.9% / 25.8% / 21.0%; Space 29.44 ns per stereo sample, 0.141%; the canvas 2.87 ms per frame at 8,192 grains (p95 2.92), inside sub-project 5's 3 ms budget (D3). The one-minute load average was 2.58 before the run, above the design's bar of 1.5, with nothing building, testing or benchmarking and only the desktop open; every in-run IQR under 3% of its median. The JSON is in `docs/benchmarks/` with a README. The ratios against the frozen `reference` kernel, 1.154 / 1.072 / 1.024, sit within 3% of sub-project 2's (carry-over row 185 closes). **(5) The carry-over's end state:** 15 open rows before sub-project 6, 13 after (185 and 233 closed); the 13 that stay open wait on upstream bumps (85, 87, 88, 218, 231), on M2/M3 features (51, 118, 119, 184, 217), on the installers (221) or on 1.0 (144, 148), each with its reason in the row. **Non-goals held:** no DSP, engine or UI change (the goldens' 22 WAV hashes unchanged, trivially); no installers or release pipeline (their own spec, next); no new CI baselines. *Decided 2026-10-29 by the director, during M1 sub-project 6; written by the Claude session that closed M1.* |
| 61 | Installers and the release pipeline decided; v1.0 moves to after M5; milestone numbering; the freeze splits | **The director's decisions of 2026-10-29,** made in the release-pipeline brainstorming (`docs/superpowers/specs/2026-10-29-release-pipeline-design.md`, approved the same day), and the docs consistency pass that recorded them. **(1) The pipeline's first job is the public releases,** not nightlies; nightlies come only because the same job on the existing schedule costs nothing. **(2) macOS builds are signed with the director's Developer ID and notarized** from GitHub Actions secrets (the two certificates as a `.p12`, an App Store Connect API key); nothing private is committed, and a pull request never sees a secret. **(3) Windows ships unsigned at 1.0** (route 1, as Surge XT, Shortcircuit XT and OB-Xf do); Azure Trusted Signing or SignPath Foundation's open-source program is the upgrade path; §21 and §22 amended. **(4) Public releases precede 1.0.0, numbered by milestone:** M2 ships as 0.2.0, M3 as 0.3.0, M4 as 0.4.0, M5 as 0.5.0, betas as `0.x.0-beta.N`, then 1.0.0; `main` carries `0.<next>.0-dev` ("that seems cleaner" than a year of `1.0.0-beta.N`). **(5) The freeze splits:** plugin identities and AU codes and the CLAP parameter IDs (with the AUv2 order that follows from them) freeze at the first public release, 0.2.0's first beta; factory preset order and the blocking CPU gate wait for 1.0.0. §18's "frozen at first release", §16, §19, §20 and carry-over rows 144 and 148 are amended; #60's "1.0, the first public release" is superseded in its second half. **(6) The pipeline extends `ci.yml`** (a `gg-installer` CMake target per leg, a `release` job), not a second workflow and not CPack. **(7) The shape:** a PKG with one component per format inside a DMG, installed system-wide; an Inno Setup installer plus a zip; a tar archive; every installer carries the notices and licenses (carry-over row 221's installer half). **(8) v1.0 moves in the roadmap to after M5 and before M6:** M2 to M5 ship as 0.x releases first, 1.0.0 is M0 to M5 polished against §10, M6 Reach follows; §20 amended, Part I untouched. A consequence the director accepted: the blocking gate and the baselines for unseen CPU models (rows 144, 148) wait until after M5. **(9) The secrets are set during the pipeline work,** so a CI dry run is part of it, and the builder may sign locally with the keychain's identities through the environment. **(10) The public nightly starts with M5;** until then the nightly keeps its signed installers as run artifacts behind a committed switch. **(11) Each release ships its complete corresponding source** as a tarball of the tagged tree with every submodule at its pin, with the SDK tags named beside the binaries. **The docs pass** (same day, `docs-polish`): INTENT's header, §8 and §12 (Part I, with the director's sign-off: overload order; the Unicode data license and the ASIO SDK's GPLv3 option in the compatible list), §14, §15, §16, §17, §18, §19, §20, §21 and §22 brought into line with the as-built record and these decisions; CLAUDE.md and README likewise. *Decided 2026-10-29 by the director; written by the Claude session that drafted the release-pipeline spec.* |
| 62 | Nothing is public, and there are no betas, before M5 | **The director's correction of 2026-10-29,** given while the release-pipeline plan's first task was running: "we should not be doing betas or considering release public until M5". So #61's "M2 ships as 0.2.0, with betas" becomes: M2, M3 and M4 are **tagged** 0.2.0, 0.3.0 and 0.4.0 and each produces a draft GitHub Release that stays unpublished (only collaborators see a draft), the director's own installers for host testing and the pipeline's rehearsals; **no betas before M5**; M5's `0.5.0-beta.N` are the first public releases, then 0.5.0 and 1.0.0. **The freeze point follows:** plugin identities, AU codes and CLAP parameter IDs (with the AUv2 order) freeze at `v0.5.0-beta.1`, not 0.2.0's; preset order and the blocking CPU gate stay at 1.0.0; the public nightly still starts with M5 (#61 decision 10). §18, §19 and §20 amended; the release-pipeline spec and plan likewise. *Decided 2026-10-29 by the director; written by the Claude session running the plan.* |
| 63 | The carry-over splits for M2 | **The director's request of 2026-10-29,** made while the release-pipeline plan ran. `docs/m1-carry-over.md` becomes M1's frozen record: a paragraph under its title says so, and nothing else in it changes after row 221's narrowing, made just before the split. Its 13 open rows (51, 85, 87, 88, 118, 119, 144, 148, 184, 217, 218, 221 and 232; row 221 narrowed to the About box, its installer half closed by the release pipeline's commits) move to `docs/m2-carry-over.md` with their numbers; that file repeats the policy's five rules unchanged and is the carry-over from now on, its new rows numbered from 300. The no-net-growth count is that file's open rows, 13 at the split. (The five rules and the count: retired by #67, 2026-10-30.) §20's M1 and M2 rows, the pointers in §14 and §15 and CLAUDE.md point at the M2 file. (A row's number is the line it stood on before the freeze; in the M1 file each now stands 5 lines lower. #60's list names the shim row 231; it stood on line 232.) *Decided 2026-10-29 by the director; written by the Claude session that wrote the release procedure.* |
| 64 | M2 planned as a program: seven sub-projects in four waves, run by agents | **The director's eight decisions of 2026-10-29,** made in the M2 brainstorming (`docs/superpowers/specs/2026-10-29-m2-program-design.md`, which places every M2 deliverable, §5 there): **(1)** one listening gate, at M2's close; the sub-projects render packs for the record only. **(2)** Structure first: layers, the parameter scopes and the mod matrix before persistence, engine scale, content and views; persistence-first was declined (it writes the state format twice). **(3)** "Everything should be M2": the Edit, Mod and FX views all land in M2, and with them §22's C++23 move (sub-project 1's first task, with a revert path) and AVX2 spike (sub-project 3, measured), the global delay, limiter, layer filter and grain buses of §14, and preset discovery. **(4)** Agents run the sub-projects, larger and in parallel where the dependencies allow: wave 1 Layers and structure alone; wave 2 Engine scale, Sources and persistence, Extremes and the FX chain in parallel (merge order 3, 2, 4, each rebased and re-verified; the order amended by #88, the mechanism by #84); wave 3 Presets and content, Views; wave 4 Close. **(5)** The program manager approves each sub-project spec; the director sees each spec's forced choices. **(6)** The live buffer's operations (overdub, loop length, splices, record/capture; §4, §5), which no milestone had dated, are M2's. **(7)** The factory macro routes target every layer. **(8)** The CC0 samples come from downloads with recorded provenance and from our own tools, in a public content repository the director creates, whose tagged zip is the release asset §12 allows. **Cross-cutting rules the sub-projects build on** (the design's §7): the per-layer chain is M1's, kept per layer, so one layer renders bit-identically to M1; the resample tap reads 256 samples late (§14); render tasks are the layers, so the M1 hashes hold (§15, §22); Source kind is a per-layer stepped parameter defined in wave 1 with its whole enumeration, and both products expose every kind; layer parameters keep scope 1's indices in scopes 2 to 4, wave 2's sub-projects append inside reserved index blocks, and the mod matrix's routings live in the patch, not as parameters; sub-project 1 alone bumps the patch version (the instrument's Macro 7 becomes Feedback there), sub-project 2 alone adds chunks (`AUDI`, `MEDI` v2 with a content-relative reference); the `AUDI` codec is sub-project 2's choice, under a criterion that every restore renders identically and matches the saved session within the goldens' tolerances (amended by #78 to what was built: every restore renders identically and restores the saved frames bit for bit; the clock and grain index restart); X keeps its definition and new benchmark cases are report-only beside it; checks that need a DAW are the close's. The carry-over should end M2 at 8 open rows (51, 118, 119, 217 and 221 closed; the rest upstream, 1.0.0 or M3; history since #67). *Decided 2026-10-29 by the director; written by the Claude session that ran the brainstorming.* |
| 65 | M2 sub-project 1 (Layers and structure): four layers and the morph, the Source kind, pitch and size spread, the mod matrix, render tasks on the host's thread pool, the patch at version 2, C++23 | **The director's five decisions** (2026-10-29, by forced choice on the spec, relayed by the program manager; `docs/superpowers/specs/2026-10-29-m2-layers-design.md` §8): **D1** option 1, the morph law is the sum-normalized vector law with the pad resting at the center (declined: the nearest corner at full level, and plain bilinear resting at corner A); **D2** option 1, the instrument gets a sidechain input port in this wave, feeding every layer's ring, with a fallback (drop the port if auval or pluginval reject an `aumu` with an input bus) that was not needed: CI's macOS leg (run on `1aa7114`) passed auval on all three identities with the instrument's input bus, and pluginval and Steinberg's validator passed too; **D3** option 3, each layer has two LFOs, two envelopes, two random sources and one 16-step sequencer; **D4** option 1, Pitch Spread is 0–24 semitones, uniform bipolar; **D5** option 2, Size Spread (0–2 octaves, uniform bipolar) joins Pitch Spread in M2, and the rest of §14's spread model goes to §22. **The program manager's acceptance** (2026-10-29; ratified by the director, 2026-11-02, #85): carry-over rows 118 and 119 move from sub-project 2 to this one, since the Source-kind parameter makes the switch reachable in both products in wave 1. **The design** (the spec and `docs/superpowers/plans/2026-10-29-m2-layers.md`, as built): C++23 for every first-party target with a GCC 13 floor that the configure enforces, the Linux leg `linux-gcc13` on ubuntu-22.04 with gcc-13 from the `ubuntu-toolchain-r/test` PPA, its baseline file renamed (§15; `6a816e9`, `3d15b06`); `gg::Engine` as one layer's engine: `EngineConfig::layer` keying every draw (layer A's are M1's), an enable gate whose re-enable forces an onset, a source-kind switch that lets the sounding grains finish on their source (rows 118 and 119), and the level accessor (`263696d`); Pitch Spread and Size Spread, `rng::Field` 5 and 6 (`db51439`); `TaskRunner`, `SerialRunner` and the tests' `StubPool` (`6038030`); the 218-parameter table, layers B to D at scope 1's indices, the next-free indices `0x31` and `0x22`, the three reserved blocks, Macro 7 "Macro Feedback" in both products (§18; `6d55d10`); the mod matrix's routes, six curves and twelve factory routes, M1's `macroOffsets` bit for bit (`af37670`); the modulation sources, libm-free (`515c1bc`); the patch at version 2, `<mod>` and the migration with the instrument's Macro 7 folded into Stereo (`a660295`); the layer-aware `ParamMap` and `builtKind` (`a9cb6e7`); the four layers in the `Processor`, the per-layer chain, the morph law, on/off, the kinds and both products' 256-sample pieces (`27f4f02`, `1235944`); `"layer"` in the grain-event line, which rewrote the 22 expected `.events.jsonl` files by that insertion alone (`88e7c8f`); one task per rendering layer on the runner, bit-identical with any pool (`fec8410`); the matrix in the `Processor`, every source stepped per grid cell and per-cell application while a time-varying route is active (`f668876`, `5d8263b`, `cd568c0`; that path's cost is unmeasured, since `gg-bench` times the engine, not the `Processor`, and is for sub-project 7's idle run or a later `Processor` benchmark); `gg-render`'s `samples`, `mod`, `controls`, `transport` and velocity keys (`fb586f7`); the plugin's sidechain port, eight remote pages, `clap.thread-pool`, the MIDI controls, the transport and the routes in the state (`f393ecd`, `8824c26`); `engine-layers` and `engine-layers-full`, report-only beside X (§16; `c5e066c`); the fourteen goldens, 36 cases in all, with `fx-layers`, `fx-mod-follower`, `inst-layers-morph` and `inst-size-spread` pinned in the click-injection table at 0, 15, 5 and 3 (§19); and the listening pack, `docs/listening/m2-layers/` (fourteen specs, a README and a host checklist), rendered for the record and heard at M2's close, which puts two questions to the director's sitting: under D1 uncorrelated layers sum quieter than one layer alone (two about 4 dB down, four 6–7 dB: should the pad keep power rather than amplitude?), and Env 1's release is barely audible in the instrument, since no grain spawns after a note-off, so the release reaches only the last grains and the reverb's tail. GCC 13's warnings were fixed in first-party code: `-Warray-bounds` by one bounded `ParamMap` accessor (`d0c0657`), `-Wmaybe-uninitialized` by a plain slot in `parseRoute` (`e0f7fb7`), `-Wrange-loop-construct` by reference bindings (`c288d3b`) and `-Wextra`'s enumerator-and-integer conditional by the test harness's transport flags of one integer type (`1aa7114`). With layers B to D off, the pad at rest, both spreads 0 and only the factory routes, every one of the 22 M1 goldens kept its WAV SHA-256 through every task. **The spec's calls that amend Part II** (its §9): each layer owns its pool of 4,096; a layer switched on speaks at once (the forced onset) and off clears its pool after the 10 ms ramp; off layers keep writing their rings; both products have four 16 s rings, the instrument's fed from the sidechain; unbuilt kinds render as the product's default kind (`builtKind`), keeping their value; Position, Spray and Feedback follow the layer's kind, not the product, so Feedback is shown in the instrument; notes act on every layer; routes may not target a macro or a modulator's control; display names carry the layer letter, with the bare M1 names as layer A's in `gg-render`. **Rulings made during the build** (IDs given here: LS, Layers and structure, then the task): LS-P1 (2026-10-29, the plan's pre-flight), `<route>` writes `target` as the target slot's CLAP ID in decimal, as `<p id>` does, never its name, since a renamed parameter must not break saved routes; `source` and `curve` by their stable identifiers, layers as `A`–`D`, `same` and `all`; LS-T1a, GCC 13 defines `__cplusplus` as 202100L for `-std=c++23`, so the smoke test's bound is `> 202002L`, and the PPA step installs gcc-13 and g++-13 itself, since the pinned `prepare-for-juce` installs no compiler (`3d15b06`); LS-T5a, the M1 listening spec `docs/listening/m1-perform/specs/07b-macro-stereo.json` no longer parses (Macro Stereo is gone), and stays as the gate's frozen record, with a test pinning its failure; LS-T7a, the random glide's time constant is Smooth / rate and the follower is a 5 ms / 200 ms one-pole with a 1e-20 flush, the tests pinning the formulas' values, and the envelope's double level and `sineDet`'s clamp to ±1 are accepted; LS-T9a, a version-1 instrument state with no Macro 7 folds M1's default 0.5, the writer leaves out a route that wouldn't read back as itself, and a later malformed `<route>` naming a valid slot empties it; LS-T10a (Task 10's fix round), the morph law gains the factor `min(1, Σ_K r_K)`, `weight_L = (r_L·b_L / Σ_K r_K·b_K) × min(1, Σ_K r_K)`, because the normalized weight kept a lone layer at 1 until its ramp was exactly 0, so switching off the last layer cut where the spec says it fades over 10 ms; one layer at r = 1 still weighs exactly 1.0 (`1235944`); LS-T10b, the event log's inter-layer interleaving follows the pieces (which end at call ends, at grid boundaries with changes, at 256-sample cuts and where an off-ramp reaches 0), so it differs between block sizes, while each layer's sequence and the audio are invariant and the goldens render at a fixed block size, so the cross-platform gate holds (§19); LS-T10c, the piece is cut where an off-ramp reaches 0, a layer's diffuser and Feedback history are cleared when it turns off, and a `setBase` of a layer's Source jumps its Feedback ramp; LS-T11a, each layer counts its own dropped grain events and `droppedGrainEvents` sums them; LS-T11b, `StubPool`'s own lock and wait are exempt from RTSan as a stand-in for a host's pool, the tasks it runs still checked; LS-T12a, `reset()` forgets the host's transport, so a reset `Processor` renders as a fresh one (`01d03e1`; the review found it kept), and `setRoutes` jumps the ramps it moves; LS-T13a, `gg-render`'s transport key is `tempoBpm` (the spec said `tempo`), the instrument's `sample` stays required and is layer A's, so `samples.A` beside it is the duplicate error, and built-in layer samples share the spec's seed; LS-T14a, the instrument's routes travel with its seed in the loaded-state snapshot and may land a block apart from its values, as PU-T5a accepts for the seed, and auval, not runnable on the build machine, is replaced there by the in-process AU test of every identity's stereo input, CI's auval being the check (`8824c26`; it passed on `1aa7114`). **The spec is amended in place** for §6.1 (LS-T1a and the C++23 library features not yet used), §6.3 (LS-T10a), §6.7 (LS-T10b, LS-T11a, LS-T11b), §6.8 (the per-layer globals as targets), §6.9 (LS-T5a), §6.10 (LS-P1), §6.11 (LS-T13a) and §12 (rows 118 and 119 closed), and the plan's Global Constraints for LS-T10a. **The carry-over:** rows 118 and 119 closed by `263696d`, 13 open rows to 11, none added. Decided 2026-10-29 by the director (D1–D5) and the program manager (rows 118 and 119), and 2026-10-29 and 2026-10-30 by Claude (the rulings), during M2 sub-project 1; *written by the Claude session that ran sub-project 1*; see §14, §15, §16, §17, §18, §19, §20, §22. **The final review's fix wave** (2026-10-30): the whole-branch review found no Critical issue and five Important ones, fixed with its minors. Ruling FR-1: a layer parameter's index is read from the X-macro's index column (`layerParamIndex`, and `slotForId` through its reverse table), never from its row's position, and scope 0's table spans `0x00`–`0x7F`, so wave 2 appends its rows inside its blocks with no placeholder rows; the next-free constants are the next free index below the blocks; and the 218-row table the plugin and patch tests both spelled out is one header, `tests/support/ParamTable.h` (§18; `2869412`). Ruling FR-2: on Linux with GCC the four products link libstdc++ and libgcc statically, their symbols kept local, and the CLAP and the VST3 export their entry points alone, since the GCC 13 floor's headers reference `GLIBCXX_3.4.32`, which a stock 22.04 lacks; CI's Linux leg checks the ceiling (§15; `4cc8230`). Ruling FR-3: a state load (an offer's values, the plugin's activation and its loaded state) sets every base with `Processor::setBases`, one recomputation of the effective values where 218 `setBase` calls made 218 (106 µs to 3.0 µs in a release build on the M5 Max), with the calls' result bit for bit but for Freeze's arming (FR-8, below) (`9cbded1`). Ruling FR-5: the grain canvas shows layer A alone (§17), and a piece's task count is 0 once its tasks are joined, so a thread-pool call outside a request runs nothing (`88230e8`). Ruling FR-6: Position and Spray's text follows Source's base value, not its effective one (§18). Ruling FR-7: Transport Playing has no golden (§19). `CLAUDE.md` now names GCC 13's four diagnostic families with their fixes and warns that two gates on one machine fail falsely, and the listening checklist says the instrument's layers B to D are silent unless their Source is Live. Every golden WAV kept its SHA-256 (36 cases); the carry-over stays at 11 open rows. Ruling FR-8 (the controller, on the fix report): `setBases` arms Freeze from the loaded values alone, every layer alike, with one `syncFreeze()` after the update (so a Freeze loaded on stays live until toggled if its layer isn't yet armed, and freezes at once if it is), since the calls one slot at a time armed layers B to D at layer A's Freeze, from their old values, and froze their empty rings at a load, #40's bug (§14). *Decided 2026-10-30 by Claude, in the fix wave, and by the controller (FR-8)* |
| 66 | M2 sub-project 3 (Engine scale and quality): the HQ sinc tier and the Quality parameter, file mipmaps, the deterministic CPU budget (weights, Budget, quotas, the overload ladder), the pool to 16,384, the sweep harness, AVX2 kernels | **The director's five decisions** (2026-10-30, by forced choice on the spec, relayed by the program manager; `docs/superpowers/specs/2026-10-30-m2-engine-design.md` §8, every recommended option): **D1** option 1, Budget is a global parameter, 256 to 16,384 Normal grains, default 4,096 (declined: a default of 16,384, which leaves the ladder unreachable in M2's ranges, and a fixed constant); **D2** option 1, equal quotas among the layers that are rendering (declined: a fixed quarter each, and the whole budget for each); **D3** option 1, a 16-tap HQ kernel (declined: 12 taps); **D4** option 1, a grain is stolen only once it has sounded half its window (declined: 90% and a quarter); **D5** option 1, only HQ grains read the mip pyramid, so every M1 golden keeps its hash (declined: every tier). **The program manager's ruling PM-1** (2026-10-30, found reviewing sub-project 4's spec): `isValidTarget` admitted a layer parameter only below `LFO 1 Rate`, which would have refused every wave-2 parameter, Quality included, as a route target; it now refuses the modulators' controls by name (`isModulatorControl`) and admits the rest, and `Processor.cpp`'s own check calls the same function (ES-T4a). **The design** (the spec and `docs/superpowers/plans/2026-10-30-m2-engine.md`, as built; §14, §16, §18, §19, §21): the HQ tier and its 16-tap sinc, bit-identical to its scalar reference (`d409d68`); the weighted ledger, per-engine quota and the overload ladder, closing carry-over row 51 (`dc9bc91`, `613fec6`); the file pyramid behind the level accessor (`2f8d9ac`) and a mip level per HQ grain (`17a8dec`, `1227aa4`); Quality and Budget in the reserved block, 223 parameters (`32151aa`); per-layer quotas, the pool size and steal events in the `Processor` (`e763924`); the steal fade (`ab69e18`, `4a2831b`); `engine-sample-hq`, `engine-sample-hq-up` and `engine-overload`, report-only (`8116629`); `"tier"` and `"lvl"` in the grain-event line and a line per steal, which rewrote the 36 expected `.events.jsonl` files by insertion alone (`9e0963f`); HQ's weight from the measured ratio (`15a1088`); the seven goldens and the listening pack, `docs/listening/m2-engine/` (`315d77c`); the AVX2 kernels (`fa3b73e`, `630d740`, `778ed29`); and the `[sweep]` family of 20 cases (`1c7b393`, `cdc73d5`). **Rulings made during the build** (IDs: ES, Engine scale, then the task and a letter): **ES-T1a**, the stretched sinc's stopband at stretch 0.5 is −37 dB (the 16 taps cover half of the window; −34 to −40 dB for β 6 to 10), so the spec's 70 dB pin there is withdrawn and the test pins at most −35 dB with the cutoff following the stretch; at stretch 1 the kernel is within 0.0007 dB to 0.3 of the sampling rate and −81.7 dB from 0.7; β 8.0 kept; the level stays `floor(log2 r)`, so a residual rate just under 2 gets the weakest anti-aliasing (a question for the director's sitting: the listening pack's `02` has a +19 step, residual 1.5); **ES-T1b**, readers carry `kFadeBefore` and `kFadeAfter`, the reach their fade is for, so the live runs' margins keep the interior where the fade is exactly 1; **ES-T2a**, rung 3's room test excludes the weight of grains already fading, so steals compound at the onset rate as the overshoot bound says; **ES-T2b/c**, the overload ladder's block-size invariance (125 s in `dev`) is `[slow]` with a fast sibling that always runs, the M1 saturation counts changed (a full pool now steals) and `bench_tests`' bound went from 98 to 101; **ES-T3a/b/c**, the half-band is 55 taps, Kaiser β 8.0, 80.2 dB (31 taps reached 51 dB), the 64-frame rule stands (1,000 frames give 4 levels), the array is `kHalfband` and its count `kHalfbandTaps`, and building costs about 1.75 s for a 10-minute stereo file; **ES-T5a/b/c**, `Engine.h` does not include `GrainKernels.h` (source slots hold `PreparedSource::Level` copies and `renderPool` builds the views per block), the swap test uses `replaceSource`, and the live kernels scale position and stretch only, the head and window staying in ring frames; **ES-T6a**, a steal of a grain with at most 256 samples left keeps its natural end and does not fade (the corner alone scored 13.86 on a sparse sine, 0 after), and **ES-T6b**, the fade's first factor is 1 within one ulp; **ES-T7a/b**, "a layer switching on takes its share at the boundary" is checked as equality with a render whose Budget halves there, and the pool test runs the FX for 1 s (29.6 s in `dev`); an instance holds about 51 MB, 6.3 MB of it event buffers (corrected by the final review: 4 layers x 4 x 4,096 x 96 bytes); **ES-T8a**, `engine-overload` keeps its definition, and with every load a multiple of 4 no lower tier fits when Normal does not, so the test pins `lowered == 0` beside `dropped > 0` and `stolen > 0`; **ES-T8b**, HQ's weight comes from the measured HQ/Normal ratio, 6.79 (6.79, 6.60, 7.43; `--quick`, block 256), so HQ is {20 mono, 27 stereo}, the mono column proportional and provisional until M5's calibration; **ES-T10a**, the spec's unit arithmetic is restated with 27, and under sustained overload the ladder mostly steals and re-admits at the draw's tier (layer B of `fx-overload` steals from 0.375 s (onset 18,011) and layer A from 0.386 s at Budget 4,096; lowering stays near 5 per 200 admissions); **ES-T10b**, a sample grain running past its sample's end is cut hard (since M0; `Engine::sampleGrain` clamps only the start), a click of 66 to 83 at Position near 1 on a sparse cloud: not fixed here, **carry-over row 300**, the remedy (clamp the admission span, fade the tail, or wrap) being a sound choice for the director, offered in the final report; **ES-T11a**, the sweep harness found three click sources and exempts two, each named in `SweepDriver.h` to be deleted with its fix: `kSampleEnd` (row 300) and `kReverbOnset` (**row 302**, Space's: raising Reverb Amount from exactly 0 under a sounding input starts the reverb from an empty room, scores 15 to 20); the third, the morph law's hand-over within a few samples when a layer switches on off the pad's center or the pad moves near an edge (1,900 at (0, 0), 37.8 at (0.25, 0.25), 56.4 with a Morph X wrap; 0 at the center), is outside this sub-project, **row 301**, owner the pad (sub-project 6) or the close, a question for the director's sitting beside sub-project 1's D1, and no committed case meets it; **ES-T11c**, §6.11's "and over `gg::Engine`" is met by the engine-level random sweep and the overload sweeps; **ES-T11d**, the driver keeps its own `kLiveGuardFrames` = 15, since `LiveSource::kGuardFrames` is private; **Task 11's fix round**, every sweep case pins a floor under its scored share, the layer-switch exemption is removed, the Reverb Size exemption applies only to a real change, and faults are planted beside the exemptions and in the rings, queue, strips and snapshots; **ES-T12a**, the kernel table is chosen per engine, so `engine-sample-sse` runs beside `engine-sample` in one process; **ES-T12b**, each function of the AVX2 unit carries `target("avx2")` and no file-level flag does, because the flag would compile shared header code with AVX2 and the linker may keep that copy (82 such functions were measured), a SIGILL hazard on an SSE-only CPU; **ES-T12c**, the spec's "spread" is the range of the per-run improvement, mixed CPU models pooled per leg; **ES-T12d**, the baselines were remade from the same three runs. **The AVX2 ruling and outcome:** a kernel path chosen at load by CPU features, whose output is bit-identical to the fallback's, is not CPU-adaptive behavior under §14; what varies is speed alone, as it already does between a NEON and an SSE build (the dispatch is decided once, off the audio thread, and the two tables are tested against each other by `memcmp` on the same grains). **Kept:** across three full-length CI runs (41111335410, 41111344500, 41111354554, at `630d740`) `engine-sample` beat `engine-sample-sse` by 13% to 35% in all 18 leg-run-block cells; the improvement cleared the spread by 8.5, 13.1 and 11.6 times at blocks 32, 64 and 256 on Linux GCC 13 (EPYC 9V45, 7763, 7763) and by 4.3, 5.9 and 2.5 times on Windows ClangCL (Xeon 8370C, EPYC 7763, 9V45); every run had `kernelLanes` 8. The x86 baselines were remade from those runs (`linux-gcc13`: EPYC 9V45 and 7763 replaced; `windows-clangcl`: EPYC 7763 replaced, Xeon 8370C and EPYC 9V45 new, EPYC 9V74 kept from the SSE-era code and stale until a run lands there); the six report-only cases entered those entries, none on `mac-universal`, and the gate stays report-only; the mac leg's `engine-sample` passed in all nine cells, and its `engine-sample-eco` and `engine-full-pool` exceeded their limits in four, the M1 VM's known drift. `GG_FORCE_SSE` is kept, and `engine-sample-sse` stays as the fallback's report-only case. **The spec's calls that amend Part II** (its §9): the plugins keep 4,096 grains a layer while an engine's ceiling is 16,384; the steal's overshoot is bounded and accepted (§14); one pyramid copy per level; the level latched at the onset and the position scaled at render; the pyramid built for every file at load. **Hashes:** every one of the 36 earlier goldens' WAV SHA-256 was unchanged through every task (the Quality default is Normal, the quota unlimited by default in the engine and far above every golden's load in the `Processor`); their event logs were rewritten once, by inserting `"tier"` and `"lvl"`; there are 43 cases. **Two rulings after the final review** (2026-10-30). **ES-F2, the idle run's regression and its fix:** Task 14's idle run at `dd27cd4` measured X (`engine-sample`) at 2.589 / 2.331 / 2.111 ns, **+6.3% / +5.5% / +3.8%** over the M1 headline's 2.436 / 2.210 / 2.034 (blocks 32 / 64 / 256), outside the 3% bar, with `reference` unchanged (the ratio rose 1.154 to 1.232 at block 32). The bisect (interleaved passes of release `gg-bench` at main `8110646`, at each task merge and at `e7952aa`) pinned the step to `17a8dec`: every renderer read a mip level at `(startPos + rate × e) × invDec`, with `invDec = 1.0 / double(decimation)` a run-time value, exactly 1.0 at level 0, so the bits stayed the same while each 4-sample step paid two extra multiplies on the chain from the position to the tap addresses, and each call one more divide. `ab0bbe5` renders level 0 through a compile-time `detail::UnitDecimation`, whose `invDecimation` is an empty `UnitScale` that multiplies to its operand: the hot loop is instruction for instruction `2f8d9ac`'s, and **all 43 goldens' WAV SHA-256s are unchanged**. The confirming in-process A/B (mean of two passes, load 5.4 to 2.5) put X's ratio to `reference` at **1.153 / 1.061 / 1.012** for the fix, against main's 1.158 / 1.109 / 1.026 and `dd27cd4`'s 1.254 / 1.153 / 1.060 (the headline's 1.154 / 1.072 / 1.024); the full idle run at `ab0bbe5` is in §16 (+1.4% / +0.8% / +1.3%, within 3%). Builds without `GG_KERNELS_AVX2` (arm64 and the SSE fallback) now call the 4-wide kernels directly instead of through the table, closing Task 12's deferred minor (an indirect call per grain, about 1% on arm64), and the engine's constructor asserts that table. The fix's review (opus) approved it with three minors, done in a later commit: the kernels' `decimation` is constrained to `uint32_t` or `UnitDecimation` (`DecimationLike`), the kernel tests run the unit decimation's run finders against the integer ones at d = 1, and the `renderPool` comments say what a non-AVX2 build calls. **ES-F3, the cancel flag:** CI on `dd27cd4` failed the mac-universal leg's "deactivate and destruction cancel a load under way at once" (run 41131723144; `tests/plugin/sample_tests.cpp`): deactivate took 452 ms and destruction 402 ms against a bound of a quarter of the load (308 ms), the load having run to its end. The cause, found by the final review's issue 6: the loader's `media::prepare` built the file pyramid (`PreparedSource::make`, 1.75 s for a 10-minute stereo file) without its cancel flag, so a cancel waited for the whole pyramid. `5f1f9b5` passes the existing flag to `make` (one line in sub-project 2's `media/src/Sample.cpp`, by addition; `make` already took the flag, and returns nullptr, which the loader treats as canceled and not as an error). It did not reproduce locally (deactivate 23 ms of a 1,510 ms load), so the fix is confirmed only by CI's mac-universal leg passing again. **The carry-over** (no count rule since #67): row 51 closed by `dc9bc91` and `613fec6`; rows 300, 301 and 302 added by the build, and 303 to 310 by the close (the deferred minors that still name a defect, and what is blocked outside this sub-project). **Questions for the director's sitting** (the listening pack's three, and F1): whether HQ earns its weight on real material; whether D4's threshold sounds right under sustained overload; whether HQ's weak spot near a residual rate of 2 is acceptable; and the remedy for the sample-end cut (row 300), with the morph hand-over (row 301) beside D1. **Amended (2026-10-30), carry-over row 300: the director's choice, fade the tail** (by forced choice on sub-project 3's final report, relayed by the program manager), with the program's rulings **EF-1** (reuse the steal fade's rows), **EF-2** (the fade must not click: its own constant, at most 12 on the click detector), **EF-6** (revising EF-1: the edge fade is a raised cosine, sin², zero slope at both ends, the FX inputs' fade shape, ruling M1-v; new kernel arithmetic accepted because a linear fade can't meet "never clicks") and **EF-8** (the steal fade takes the same shape: its linear end scored up to 50). A sample grain whose read position would pass its sample's last frame while its window is open now ends there: `Engine::admit` latches the first output sample at which `startPos + rate × e`, the reference's position in level-0 frames, exceeds `frames − 1` (`grainEdgeSample`; the last frame itself for every tier and level, so the end doesn't depend on the tier the ladder admits), and `GrainPool::spawn` ends the grain at it, faded out over the `kEdgeFade` = 256 samples before it, or over all it has when that is fewer. No grain reads before frame 0 while its window is open (the start is clamped into the sample and the rate is positive), so the start has no edge; the ring's grains have none. **Both fades read the Hann table's rising half** (built at construction, no libm on the audio thread), at float64 phases from latched integers: the steal fade `H((end − t) / 512)` (`invFade` still 1/256; the steal sample stays at 1, ES-T6b), the edge curve `H((edgeEnd − t) × 0.5 / L)`, in `detail::stealFaded` and the new sample-only `detail::sampleFaded`. Every fading sample goes through the scalar reference, so the 4-wide and AVX2 kernels didn't change and still match by `memcmp`. A cut grain keeps its weight until its cut end, not as fading weight, and may be stolen once; the steal fade multiplies its edge curve, so the gain is continuous. **Scores** (gains 1 and 4, rates 0.5 to 4, every tier, 16 edge or steal phases): the edge curve at most 2.81 at 256 (the uncut cut scored 25 to 3,695; a linear fade's end corner about 50 at any length, which is why EF-6 overturned EF-1); the steal fade at most 2.84 (sample) and 3.07 (live), against the linear fade's 50.35 and 49.68. **Goldens:** 41 of the 43 keep their WAV SHA-256; `fx-overload` and `inst-overload` move (their 3,374 and 364 fading steals; event logs unchanged, click scores unchanged at 0.00 and 3.11, their spectrograms' steal smear much lower); the new `inst-sample-end` (44 cases) cuts all 34 of its grains, 1.94 against 36.03 uncut. The sweep harness's `kSampleEnd` exemption is deleted and the floors it lowered raised; the instrument extremes sweep scores 14.50 (corner 2's own material, not a fade); the instrument's Quality overload sweep plays 1 s grains from the sample's start. A grain that `setSource` or `replaceSource` moves onto another source keeps the edge of the one it started on. Commits: `65292dc` (the fix and its tests) and `0d23734` (the goldens). *Decided 2026-10-30 by the director (D1-D5) and the program manager (PM-1), and 2026-10-30 by Claude (the rulings), during M2 sub-project 3; written by the Claude session that ran sub-project 3; the row 300 amendment 2026-10-30 by Claude, sub-project 3's follow-up* |
| 67 | The carry-over count rules are retired | **The director's decision of 2026-10-30,** during M2 wave 2: deferred items are handled as superpowers' subagent-driven development handles them. A reviewer's minor is fixed or deferred at the controller's judgment; deferred minors and items blocked outside our control are listed in the sub-project's final report and logged in `docs/m2-carry-over.md`, with no "no net growth", "touch it, close it" or count rule, and nothing filed upstream. #60's and #64's carry-over targets are history, and so are #50's carry-over policy (its no-upstream-filing rule stands) and #63's five rules and no-net-growth count (named here by the M2 close's consistency pass, 2026-11-02, #89). CLAUDE.md's "Working style", the M2 carry-over file's header and the program design's §8 say so. *Decided 2026-10-30 by the director; written by the program manager session.* |
| 68 | M2 sub-project 2 (Sources and persistence): the Resample source, rings of 1 to 300 s with loop, overdub, splices and capture, the memory cap and the sample pool, `MEDI` version 2, `AUDI` as float32 WAV, `INST`, the captures folder, rings saved and restored, collect and save | **Built** (2026-10-30; `docs/superpowers/specs/2026-10-30-m2-sources-design.md` and `docs/superpowers/plans/2026-10-30-m2-sources.md`, PR #19): Tasks 1 to 9 built, reviewed and merged, the goldens and listening pack (Task 10) and this documentation (Task 11) beside them; the ring pyramids (Task 12) after sub-project 3, merged into this branch from `main` on 2026-10-30 (a merge, not a rebase, the program manager's ruling, ratified by the director on 2026-11-02, #84), then Tasks 12a and 12b, the final review and its fix wave (Task 13, 2026-10-31). **The director's seven decisions** (2026-10-30, by forced choice on the spec, relayed by the program manager; the spec's §8): option 1 of each. **D1**, a session saves a ring when it is held, its Freeze or Loop on; a plain live ring isn't saved (declined: every Live and Resample ring, which fills the embed cap with transient audio; no ring, which leaves #40's frozen pad unheld). **D2**, the memory cap defaults to 1 GiB, floor 64 MiB, ceiling 16 GiB (declined: 2 GiB, which lets ten instances claim 20 GB; 512 MiB, which one long ring and one long sample exceed). **D3**, the longest ring is 300 s, also the top of Loop Length and of Position's widened range (declined: 600 s, 460 MB a ring with levels; 120 s). **D4**, the Resample source hears the instance output as the listener does, the dry signal, the reverb's return and, from sub-project 4, the limiter (declined: the layers' sum before the mix). **D5**, Loop on an unfrozen ring is a sound-on-sound looper, Overdub recycling each frame leaving the window (declined: Loop stopping the head as Freeze does). **D6**, splices are numbered from the oldest mark (declined: from the newest, which renumbers on every new mark). **D7**, the default captures folder is `~/Music/Giga Grains/Captures` (declined: inside the sample folder). **The program manager's approval** (2026-10-30): the spec against the program design and INTENT, with three notes: this row takes the next free number at merge; the mod matrix's target rule for wave 2's layer parameters is sub-project 3's, so this sub-project tests that Loop Length, Overdub and Splice are admitted after the rebase rather than implementing it (until then Ring Length is admitted as a target too); and `gg/media/ContentPaths.h` is sub-project 5's, `findSample` taking the content roots as an argument. **The codec call and the libFLAC amendment** (the spec's §5 and §9, confirmed by the program manager at the approval): `AUDI` payloads are float32 WAV bytes, not FLAC, since float32 restores bit for bit, which exceeds the program design's criterion (every restore renders identically and matches the saved session within the goldens' tolerances; the criterion amended by #78 to what was built, which float32 meets: the frames restore bit for bit and every restore renders the same), a ring's floats beyond ±1 need no scaling, one representation serves embedded and filed audio, and a dependency that only encodes isn't worth building on three platforms; the cost is about 2.5× 24-bit FLAC's size, so the 32 MiB embed cap holds about 87 s of stereo at 48 kHz. §18's "as FLAC" is amended and §15's planned libFLAC row removed; the program design's exit "libFLAC is pinned" is met by not needing it. **The design** (as built; §14, §15, §16, §18, §19, §21): `RingStorage` apart from `LiveSource`, the window `[E − W, E − 1]` generalizing N, Overdub with its period, 32 splice marks in a fixed array and the `AddSplice` and `ClearSplices` commands on the grid (Task 1); the five ring parameters at `0x..40`–`0x..44` in every layer scope, 243 parameters in all with sub-project 3's five, Resample built (`builtKind(Resample)` is Live in both products) and Position's CLAP range widened to `kPositionMax` with no M1 value moved (Task 2); `MEDI` version 2, `AUDI`, `INST` and float32 WAV bytes (Task 3); the memory budget, the sample pool and the captures folder setting, the 600 s cap removed (Task 4); the `Processor`'s Resample tap, ring parameters and commands, the ring exchange and the ring read (Task 5); a ring's audio saved and restored, the content roots and the captures folder in the search, and `collectMedia` (Task 6); `MediaSession`, four loaders per instance in both products, the rings saved and restored, the Ring Length watcher, capture and collect and save (Task 7); `gg-render`'s `state`, `--save-state`, `operations`, `memoryCapMiB`, `--content-dir`, `--captures-dir` and the two generated state fixtures (Task 8); and `engine-live-long`, report-only beside X (§16; Task 9). With Ring Length 16 s, Loop off, Overdub 0, no splice, no Resample layer, nothing embedded and the cap at its default, every one of the 36 goldens kept its WAV SHA-256 through every task. **The spec's calls that amend Part II** (its §9): the tap hears the output (D4) and costs nothing while no layer is Resample (amended by SP-T12ac); Ring Length is resized by the main thread through the exchange, the audio thread's copy bounded at 8,192 frames, and so is neither automatable nor modulatable; a restored ring arms Freeze, so a reopened frozen pad is held from the first sample, while an unsaved ring keeps "live until toggled" (§23 #40 amended; FR-8 unchanged); Resample is a Live-mode engine fed by the tap; the window length W generalizes N, so loop and splice share the fade and the admission clamp (amended by SP-T12aa: the admission clamp alone); Overdub's period is the loop with Loop on, else the ring; capture reads on the main thread without stopping the audio thread; one `AUDI` chunk with ids; the memory cap in `INST`, a setting rather than a parameter; the cap counts an instance's references in full, shared or not; the pool holds weakly; the 600 s cap is removed outright; `findSample` takes the content roots as an argument; collect and save is a `gg-media` function plus a plugin command; four loaders per instance in both products, the FX writing `MEDI` entries; `gg-render` cuts its calls at a capture's frame; the restore goldens' state files are generated, never committed; one benchmark case. **Rulings made during the build** (IDs given here: SP, Sources and persistence, then the task; SP-T1a to SP-T1d are the build ledger's R1-1 to R1-4): SP-P1 (the plan's pre-flight), the engine's and `gg-core`'s `kMaxSplices` are both 32, checked by a `static_assert`; SP-T1a, a mark's aging and a selected splice's fallback are decided per sample from the head alone, never per block, so block-size invariance holds; SP-T1b, under D6 the Splice request stays the caller's value and counts from the oldest mark still in storage, a request past the count is the last splice, and the selection falls back to the whole window only when no mark is left; SP-T1c, a frozen restore comes back with the head stopped and the write gain 0, held from the first sample with no ramp; SP-T1d, an ownership exception the program manager was told of: `GrainKernels.h`'s `liveRun` (sub-project 3's file) takes the window's end and oldest frame from `LiveSource`, a minimal edit by addition, since the SIMD live kernel assumed the full ring's margin and, under a loop or a splice narrower than the ring, could differ from the scalar reference by one bit (after days of continuous running), and no edit inside `source/` could restore the identity, with `[kernel]` tests under a loop and a splice; SP-T2a, `kPositionMax` is 1.5279342f (the plan's 1.5279259f was a miscalculation); SP-T2b, typing "100%" into a Sample layer's Position parses to 1.0, not `kPositionMax`, so a later switch to Live reads M1's 16 s behind, not 300 s; SP-T3a, an entry naming a missing `AUDI` payload loads with its layer's status *embedded audio missing* and the rest of the state, since an older build's re-save keeps `MEDI` and drops `AUDI` (§23 #53's intent; the cost, a corrupt state loads with a status instead of refusing); SP-T3b, beside a newer `MEDI` the `AUDI` chunk is kept verbatim and written back; SP-T3c, a payload's WAV header is 44 bytes mono and 68 stereo (extensible), not the spec's 48; SP-T3d, a `<ring>` has no `channels` (a ring is stereo) and no `head` or `loop` (the layer's parameters carry them), and more than 32 marks fail the state; SP-T4a, the pool's find and insert are one step under its lock, so two loaders building one key share the first, and a source prepared at another rate is refused; SP-T5a, the ring read's check is `R₁ − max(0, R₀ − W) ≤ N`, the window's length, not the spec's "< C", since up to one block is written past the published head, and the reader copies, issues an acquire fence, then reads `R₁` and the epoch, taking W from `ringMeta` and `R₀` from the head atomic (the meta can name a previous ring's head); SP-T5b, after a resize the engine's Position and Spray reach the new ring (`Engine::ringResized`, added to the shared `Engine` by addition); SP-T5c, Loop, Loop Length, Overdub and Splice act on a layer's ring whatever its Source kind, a Sample layer's ring recording for a later switch, and only the commands are ignored on Sample layers (one rule for the ring, no per-kind gate on the audio thread; the cost, an Overdub left up on a Sample layer surprises on a switch to Live, listed for the director); SP-T5d, at most 64 commands apply per boundary group (`kMaxGroupCommands`), later ones ignored; SP-T6a, a restore doesn't hash an embedded payload again (the container is read all or nothing; a filed ring's hash is verified by the search), the cost being that a corrupted payload of the right length restores as noise; SP-T6b, a restored ring's marks scale by its WAV header's rate, as its frame count does; SP-T7a (the program manager), `params.flush` adopts no ring, since "params.flush stays trivial" is a non-negotiable: an offer carrying a ring is taken whole in a flush and applied, values, source and ring, at the next `process()` call's start; SP-T7b, a source draining in the exchange isn't counted against the cap, so a refusal never depends on when a retired source is collected (a spec amendment; the cost, an instance briefly holds one sample's bytes past its cap for a grain's length); SP-T7c, a save of a running ring keeps W frames, writing silence over any front the head lapped, as the resize's stable copy does, so the loop's period and the marks' offsets survive; SP-T7d, the fingerprint cache applies to frozen rings alone; SP-T7e, a restored session renders deterministically, the same on every restore, but not identically to the saved session continuing, since the clock and the grain index restart at 0; SP-T7f, collect and save saves first, then collects every file that save references, filed rings included; SP-T7g, an activation clears only a save's *not saved* messages, and a restore's *ring not found* stays until the next restore or until its layer is no longer held; SP-T8a, a saved ring is the loop window, `min(L, N)` frames with Loop on, else `min(head, N)`, with every mark inside it, the Splice selection not narrowing a save (it is a saved parameter and reselects on restore), and a capture follows the loop window, then its `splice` argument, k counting from the oldest mark inside the window (the cost, a save of a spliced ring keeps more audio than the selection, never less); SP-T8b, that window comes from the Loop and Loop Length base values, as D1 decides by the bases, so a capture while Loop Length is modulated takes the base length; SP-T8c, `gg-render`'s state keys: the spec's seed is the render's, not the state's; built-in signal samples aren't saved by `--save-state` (no file to name); a capture's `file` is relative to the working directory, like `-o`; an instrument spec may leave out `sample` beside `state`; Ring Length is set in `params`, never `automation`; SP-T9a, under `--quick` every engine case's warm-up is capped at 16 s, the longest any other case uses, so only `engine-live-long`'s quick run changes, and its full-length run keeps the 300 s fill. **Task 12, the ring pyramids** (2026-10-30, after sub-project 3 merged into this branch; §14's Mipmaps): the controller's rulings **SP-T12a**, the live kernels keep the head, window and fade in ring frames and take only their taps from a level, through a level path added beside sub-project 3's level-0 one in `GrainKernels.h` and `GrainKernelsAvx2.cpp` (an ownership exception like R1-4; the per-level view with its head in level frames was rejected, its fade 2ⁿ times longer and its gain near the head about 0.42); **SP-T12b**, three levels, not four: with the 55-tap half-band the ≤ M rule builds levels 1 to 3 (level 4's lag is 405 frames); **SP-T12c**, Ring Length is not a mod-matrix target (`isValidTarget` refuses it: neither automatable nor modulatable), sub-project 3's test that admitted it corrected. **SP-T12d** (2026-10-30, the review's fix rounds): a resize **copies** the ring's level frames as it copies its frames (`copyLevels`: the main thread's stable part, then the audio thread's at most 8,192 newest), because computing a long stable part's levels on the main thread (73 ms for 60 s, 368 ms for 300 s) outlasted the 170 ms in which the audio thread writes the frames an adoption may copy, so every offer of a long ring was declined; a restore **computes** them from the restored frames with ring time restarting at 0, so they equal the saved ring's bit for bit when the saved window began at frame 0 and otherwise hold the same audio away from the oldest ±lag frames, on the restored frame 0's decimation grid. Two plugin tests pin it: a 300 s ring resized to 200 s and back while the audio thread runs in real time (`[serial]`, bounded by work), and the adoption bound itself, which lands with exactly 8,192 frames written since the copy and declines one block more. `fx-hq-live-level0` is regenerated as `fx-hq-live` (every line `"lvl":1`, the fold-back gone, its click-table entry 9 to 10); every other golden's WAV kept its SHA-256. **Task 12a, no click at a window change or a Resample switch** (2026-10-30, the program manager's condition before the build is reported ready; the brief's two defects, found by Task 10's listening pack and by the main merge's `[sweep]` harness): the controller's rulings **SP-T12aa**, the ring's window steers admission alone and every read and the head-proximity fade keep the storage's bounds `[R − N, R − 1]`, so a grain admitted under one window finishes in its old span when a Splice change, Loop on or off or a Loop Length change narrows, moves or widens it (the brief's mechanism (a); (b), the window's ends moved over a ramp of the fade's length, was declined because a grain the ramp outruns still steps, a jump of D frames fading it in M²/D samples, and the new window comes late); `liveRun` and its level twin keep the window's bounds, a subset of the storage's, so the SIMD kernels match the reference bit for bit unedited; **SP-T12ab**, a switch of a layer's kind to or from Resample crossfades its ring's input between the instance input and the tap over `kRampSeconds` (10 ms, the length of every ramped value, Feedback's and Overdub's among them) from the boundary, the tap alone where the ramp is 1 and the input alone where it is 0, bit for bit, and `setBase()` or a loaded state jumps it; **SP-T12ac**, the tap's history is written whatever the kinds, two stores a sample, so a switch reads the output 256 samples late at once (the spec's "costs nothing while no layer is Resample" amended). Measured with the goldens' click score: a dense cloud (32 Hz of 50 ms) under splice and loop changes at every tier and ring levels 0 to 2, six seeds, 84 to 2,955 before and at most 2.4 after; a Live → Resample → Live switch, four seeds, 37.5 to 516.9 before and 0.0 after; the listening pack's `06` at 8 Hz, 262.0 before and 1.8 after; the `[sweep]` harness's Source sweep 990.7 before and 2.7 after. `fx-splice-capture` is regenerated at 32 Hz so both its Splice changes fall under sounding grains (165.5 on the engine before the fix, 3.09 after; its click-table entry 14 to 12), the listening pack's `06` and `07` at 8 Hz; every other golden's WAV kept its SHA-256. The spec's §6.1, §6.2 and §9 are amended in place. **Task 12b, a ring shrink waits for its grains** (2026-10-30, the program manager's condition, from 12a's flagged risk measured by its review: 16 s to 4 s under dense grains reading 6 to 8 s behind scored 275 to 2,754, a grain reading older than the new length being cut in one sample at the swap): the controller's ruling **SP-T12ad**, a two-phase shrink. First, from the Ring Length's applied boundary the audio thread limits admission to the newest N′ frames less `kCapSlack` = 64 (the sinc's 63-frame reach at level 3), Position and Spray reaching N′, and narrows the reads to N′ at the first sample from which no sounding grain would step at the change (`Engine::setRingLimit`: checked at once, else at the end of the latest grain that reads farther, again there, each check at its own sample, a block starting there, so block-size invariant; a splice whose start the limit binds takes the head's run for its oldest frame, so its grains don't drift past the limit); the limit stays in force after the swap, until the request changes, so admission is the same before and after it. Second, the plugin's watcher copies and offers the shorter storage only once `RingMeta::readFrames` fits it, *resizing* meanwhile, and the audio thread declines a shorter ring than its reads reach (`Engine::liveRingFits`). **Its fix round** (2026-10-30, the review's findings, each a ruling of the program manager): the read limit lands only once no sounding grain can reach past it for the rest of its life (its position and the head linear between now, a freeze ramp's stop and its end), since a pitch-down grain that fitted at the instant drifted into the window fade later; admission and Position's bound never reach past the reads, so a Ring Length raised again after the reads narrowed lifts them only when the reads widen (before, the lift could wait for ever under dense pitch-down grains, the older audio silent); overdub's period P, never longer than the reads, changes at the landing crossfaded over 10 ms, and the ring needs the old period until that ends (`RingMeta::framesInUse`, renamed from readFrames), so with Overdub the output no longer depends on when the swap lands; the slack ends at the landing, so a shrunk ring admits as a fresh one of its length (§18's reopen); and a ring shorter than the length last asked for is declined, so a stale offer can't land after a grow back. **Its fix round 2** (2026-10-30): a landing waits until the overdub period's crossfade from the last one has ended, then takes the request in force there (a drag lands its latest value), since restarting the crossfade from the old length stepped the frames written (two changes 3 ms apart scored 2,386 on the ring, a drag 337). The Processor gives its engines a ring limit only from a change, not at construction. Measured with the goldens' click score: a `Processor` shrink 16 s to 4 s at 32 Hz of 50 ms, offered at four different times, 125 to 3,011 before and 0.0 after, the same bytes at every offer time; the engine's 16 s to 4 s (Eco, Normal, HQ at levels 0 and 1), 60 s to 16 s and a grow 16 s to 60 s, two seeds, the same bytes whether offered at once or 0.5 s later and in calls of 64. Every golden's WAV kept its SHA-256. The spec's §6.5 is amended in place. **The final review's fix wave** (2026-10-31; the review found no Critical issue): D1 saves a held ring only on a layer whose base Source builds to Live, Live or Resample (`gg::core::ringSaved`, shared by the plugin and `gg-render`), since a frozen Sample layer saved a ring no grain reads, 6.1 MB a layer at 16 s (the controller's ruling, ratified by the director on 2026-11-02, #82; the cost, a Sample layer switched back to Live after a reopen starts with an empty ring); Freeze is on above 0.5 everywhere (`gg::core::freezeOn`); a save while a state's restored ring waits for the audio thread writes that ring, not the one still playing; a deactivation keeps the held rings' frames and encodes nothing on the host's main thread (four 60 s rings: 2.2 s in Debug before, 0.18 s after), the next save encoding them and the next activation restoring them with no decode; a whole-window read the head laps keeps its first try, not the last of eight; the audio thread's ring head store is followed by a release fence; and CI's sanitizer job's timeout is raised from 60 to 90 minutes (the program manager's ruling), since main's run took 48 minutes at `1d62946` and this branch's about 66, its ring tests under ASan and RTSan; splitting the job into an ASan job and an RTSan job is a carry-over row, for sub-project 7. The test "a replaced ring stays published until its storage can be collected", which predated SP-T12ad's declines and stalled under RTSan on CI, now keeps its grains inside the shorter ring and bounds its waits by the audio thread's work. The deferred minors are rows 339 to 352 of `docs/m2-carry-over.md` (numbered 312 to 325 until the merge with sub-project 6's PR 1, whose rows took those numbers first). **Left to later sub-projects:** the UI for all of it, the memory readout, the strips and the ring gestures (6), which also owns the live peaks' 16 s after a resize; the content folder's layout and roots (5). **The spec is amended in place** for §6.1 (SP-T1a, SP-T1b, SP-T5c), §6.3 (SP-T2a, SP-T2b), §6.4 (the `AddSplice` anchor and SP-T5d, SP-T5a, SP-T8a), §6.5 (SP-T7b, SP-T7a, SP-T5b), §6.6 (SP-T3a to SP-T3d, SP-T8a, SP-T8b, SP-T7c to SP-T7e, SP-T6a, SP-T6b, SP-T7g), §6.10 (SP-T8c), §6.11 (SP-T9a), §12 (the count rules retired) and §13 (as made), then by Task 12a for §6.1 (SP-T12aa), §6.2 (SP-T12ab, SP-T12ac) and §9 (SP-T12aa, SP-T12ac), and by Task 12b for §6.5 (SP-T12ad), and its status reads Built. **The carry-over:** none of the 11 open rows of `docs/m2-carry-over.md` is this sub-project's (the spec's §12); the build's deferred minors are logged there as plain rows after the final fix wave (the director has retired the count rules). Decided 2026-10-30 by the director (D1–D7), the program manager (the approval's notes, the codec amendment and SP-T7a) and Claude (the rest), during M2 sub-project 2; *written by the Claude session that ran its documentation task*; see §14, §15, §16, §18, §19, §21. **Amended (2026-10-31), the merge of presets group 1 (§23 #70) into this sub-project** (rulings made by Claude resolving the merge; no design of either sub-project changed): **SP-M1**, presets' `findStateChunks`, the container walk `decodeState` and `presetInfoOf` share, also finds AUDI and INST and fails a repeated one, so both read a container alike; **SP-M2**, a preset load (`applyState`, `LoadKind::Preset`) takes D5 per layer: each layer the preset names no sample for keeps its own, entry and source, a layer it names one for restores it as a session does, and the rings, their statuses and the memory cap stay as they are (`MediaSession::restorePreset`); a preset's INST is never applied; **SP-M3**, "save as" names every layer's sample file, leaving out an embedded sample (a capture), the rings and a newer build's manifest, and writes INST with the default cap, ignored on load; **SP-M4**, a render spec with both `preset` and `state` (or `--preset` beside a spec's `state`) is bad input, since a state holds a patch of its own; **SP-M5**, the settings carry both `capturesFolder` and `contentFolder` (and, after the views merge below, `theme`), and `saveSettings` refuses a NUL in any of its strings (the five of them after that merge); **SP-M6**, the content roots stay unwired into the sample searches, which presets' group 2 (its Task 14) passes, and `gg-render` still refuses a preset that names a sample. The session and INIT byte pins (`tests/core/preset_tests.cpp`) were retaken (the twenty ring parameters and the container's MEDI version 2 and INST), and Mutate's default-lock counts follow the five ring parameters per layer, which stay unlocked until presets' group 2 extends the locks. Then the merge of sub-project 6's PR 1 (§23 #71): **SP-M7**, the five ring parameters take rows 1 to 5 of the Edit view's Source panel, after Source (Ring Length, Loop, Loop Length, Overdub, Splice), as the smallest change that keeps every parameter reachable (after VW-T19a; the ring's gestures and the memory readout stay PR 2's); the settings' `theme` sits between `capturesFolder` and `contentFolder`, and `saveSettings` refuses a NUL in any of the five strings; `gg-snap` links `gg::media`, since MediaSession.h reaches it through GigaGrainsPlugin.h; this sub-project's carry-over rows 312 to 325 became 339 to 352, PR 1's having taken those numbers first. Every one of main's 44 goldens but `fx-hq-live-level0` (regenerated here as `fx-hq-live`) kept its WAV SHA-256 through both merges, and this sub-project's eight kept theirs; there are 51 cases. **Amended (2026-10-31), the merge's review: SP-M8**, a Mutate press never moves a parameter no host may automate, Ring Length the only one (`withFlags(0u)`): `mutate()` copies a slot whose flags lack `CLAP_PARAM_IS_AUTOMATABLE`, with no draw, since a press is host gestures recorded as automation and a Ring Length jitter would resize every on layer's ring on the main thread (up to about 115 MB a layer, memory refusals, and a shrink cutting a frozen ring's oldest audio); the default locks are unchanged (Ring Length stays unlocked, and still unmoved). The presets owner's sign-off on SP-M8 is pending (signed off and extended by #70's ruling (4): the five ring parameters are structural locks). **Amended (2026-10-31), the views owner's request: SP-M9**, each layer's Ring Length control in the Edit view's Source panel commits on release, as D10 does Reverb Size (`setCommitOnRelease(true)` in `ParamPanel`, with a tooltip saying the ring resizes once, on release), since a Ring Length change is a ring resize (a new storage of up to about 115 MB a layer, against the memory cap) and a drag would allocate at every step; Loop, Loop Length, Overdub and Splice stay continuous; the test "each layer's Ring Length commits on release and its neighbors do not" pins it. *Amended 2026-10-31 by Claude, the merges and the merge's review* |
| 69 | M2 sub-project 4 (Extremes and the FX chain): Smear, the Spectral Smear source, two filtered grain buses per layer, a global delay and a global limiter | **The director's eight decisions** (2026-10-30, relayed by the program manager; spec `docs/superpowers/specs/2026-10-30-m2-extremes-design.md` §8), each the recommended option 1: **D1** the Spectral source smears the layer's sample when it holds one, else its live ring; **D2** Freeze holds the material, not the smear (a spectral freeze; the smear keeps evolving); **D3** grains split across the buses by chance, Bus Split the share on bus 2; **D4** the bus filter is a 12 dB per octave `CytomicSVF`; **D5** the limiter's stronger mode is Peak, a zero-lookahead peak limiter; **D6** Delay Time glides at no more than half a sample per sample; **D7** Delay Feedback reaches 100%, the loop soft-clipped, and the instrument's tail stays 122.5 s; **D8** Smear at 100% is full strength (overlap toward 32, jitter ±half a grain, detune ±50 cents). **D7a, the director's decision of 2026-10-30, option 2:** Damping 0 keeps the damper (still a 20 kHz one-pole in the loop), so at Feedback 100% the hold slowly darkens bright material: a noise burst loses about 2 dB in the 2–4 kHz band over 20 s, and a sine burst holds within 0.1 dB (declined: Damping 0 bypassing the damper, for an exact hold). **Rulings made during the build:** the Spectral synthesis divides by 0.75 (`kOverlapGain`), not the spec's 1.5, because frames with redrawn phases add in power; the phases are keyed per channel (stream = the channel), so the channels decorrelate fully; switching away from Spectral drains the pending overlap-add, then writes silence (the no-click anti-goal binds); Position on ring material is bounded by the live ring as on a Live layer, with a floor of 1; the STFT keeps running while a Spectral layer is off (CPU only; deferred); the material is decided where the engine adopts a sample, not where one is offered; the sticky bus-2 flag is raised at the first bus-2 admission and lowered by `reset()` and the enable gate; Peak's gain is computed in double and a mode change crossfades all three laws; the per-grain-filter figure (§22) is an upper bound, never a result; `inst-filter-lfo` runs over noise (a sweep at a fixed Position is one tone) and `fx-spectral-freeze` at Position 0.28, and the click table pins seven of the nine new cases; `engine.cloudParams()` and, since the M2 close (row 381), `SpectralSource::lastRingFrameStart()` are read-only accessors kept for the tests. **The §9 calls that amend Part II:** the Spectral kind's Position, Spray, Freeze and Feedback rules; the limiter's Off default; the spectral ring's 5 s; the `Field` numbering at the rebase, in merge order. **The design** (as built): Smear, `rng::Field`s `SmearJitter`, `SmearDetune`, `Bus` and `SpectralPhase`; the 40 parameters in the `0x60` blocks (258 in all, the patch still version 2); pffft in the engine; `CytomicSVF` in `gg-core`; the delay and limiter as units beside Space; thirteen remote pages; nine goldens, the comb test, and every earlier WAV hash kept. §22's buses question is resolved with the measurement and two new rows. Benchmarks: `engine-sample-smear`, `engine-buses`, `engine-spectral` and `--chain`, report-only. **The idle run** (2026-11-01, `cc68e3d`; `docs/benchmarks/`): X is unchanged within ±0.4% (an A/B of main and this head in one window), the three new cases and `--chain` (0.057% of a core) are published in §16, and the per-grain filter's added cost is 2.75× to 3.59× X per grain-sample, far above a quarter of X, so buses with channel masks are v1's answer and per-grain filters wait for M5's budget weights (§22). **The final review's fix wave** (2026-10-30; the whole-branch review found no Critical issue and two Important ones, fixed with its triaged minors): Delay Ping-pong ramps over the 10 ms as a weight crossing the damper's input between the channels' reads, exactly today's law at 0 and 1, since a hard switch stepped every later echo; and every new unit snaps where the ramps jump: the limiter's mode also on a macro's or route's move, and a bus filter's type on its first cell after a clear (a new instance, a reset, the layer switched back on), which moved `inst-filter-lfo`'s first 10 ms by at most 0.0027 (its event log unchanged; the coordinator's ruling, a golden of this branch, never published), every other WAV hash kept. Smear and four Spectral layers over their rings joined the block-size and thread-pool invariance tests (§19). **The reverb click** (the program manager's, from sub-project 3's sweep harness; carry-over row 311): Reverb 2 set its modulation depth (up to 240 samples of each line's read) once per 32-sample block, and `SpaceReverb` passed Modulation straight through, so a jump scrubbed the lines at up to 8.5 times speed, echoed at full level at Decay ∞ and Damping 0. **Ruling:** Reverb Modulation glides, 0 to 1 in 30 ms (`kModulationGlideSeconds`, as long as the patch's glide into ∞), on the chunk grid, and an empty reverb (constructed, reset or cleared) takes the new value at once, so the idle rule's fresh-instance contract holds; `Reverb2Patched.h` is untouched (the program manager assigned `core/src/space/` to this sub-project for it). The cost, a listening item: a full Modulation move bends a held tail by about 3 semitones for the 30 ms. The sweep harness's `kReverbModulationDrop` exemption is deleted, and row 311 is closed. **The merge of main** (2026-10-31, Phase C): main's sub-project 3 and its follow-up came in as one merge, not a rebase (ratified by #84); sub-project 3 added no `rng::Field`, so this branch's fields keep 9 to 12; the forty parameters follow Quality in each layer and Budget in scope 0, **263 in all**; every one of main's 44 golden WAVs and this branch's nine kept its SHA-256, and the nine event logs gained main's `tier` and `lvl` fields alone. **The merge of sub-project 2** (2026-11-01, Phase C; main at `4fed5bb`, PR #19): one merge, not a rebase (#84); sub-project 2 added no `rng::Field`, so this branch's keep 9 to 12; its five ring parameters precede Quality in each layer's rows and the forty follow it, **283 in all**, 62 per layer, no ID moved; the extremes' carry-over rows are 357 to 380, past sources' 339 to 356. Of main's 51 golden cases, 49 render bit-identically on the merged head, and fx-layer-switch and fx-overload differ by row 301's morph fix alone; every case this branch shares with the merged head, the nine among them, kept its SHA-256. The FX extremes sweep's scored-share floor is re-pinned from 0.41 to 0.395 (it measures 0.402, and 0.413 with the ring parameters held at their defaults): at their ends, Overdub 1 among them, the rings are louder, so Peak is driven under −6 dB longer and kLimiterDrive, unchanged, covers 12.8% of the render against 11.6%; no exemption was added or widened. **The output bound with the delay** (the director, 2026-10-31, D-bound option 1): §14's bound stays 2 + the input's peak, Space's; with the global delay engaged (Delay Amount above 0) it is 3 + the input's peak, since the delay's return, capped near 1 by its loop's soft limiter, adds to the wet signal; the limiter's Soft and Peak modes keep the output under their ceiling. The sweep harness checks it so. **The sweep's two limiter and delay exemptions** (the director, 2026-10-31; carry-over rows 378 and 379, closed as accepted laws, not defects): **D5b, option 1:** D5's limiter laws stand (zero lookahead, no attack), and the sweep harness's `kLimiterDrive` looks away over the calls in which Peak's gain falls below −6 dB or Soft's input passes twice the ceiling, plus 10 ms after, and nothing wider (declined: an attack or lookahead for Peak, a knee for Soft; the click entering Peak mid-sound, inside the mode fade, is part of the same law: #79). **D-glide, option 1:** Delay Time's glide stays tape-like, and `kDelayGlideHold` looks away only while the delay's effective Feedback is 0.95 or more and its read time is still gliding, on the samples where Delay Amount is above 0 (declined: holding the glide, or crossfading taps, at the top of Feedback). Row 301's morph handover is not exempted. **The morph handover** (carry-over row 301, assigned to this sub-project by the director, 2026-10-31): the morph law divided each rendering layer's r_L·b_L by Σ r_K·b_K, so a ramp near 0 on a dominant corner outweighed the rest (a layer switch at the pad's (0, 0) moved a weight by 0.9995 in one sample, a click of 547 over sines; the FX extremes sweep's corner 17, echoed by the delay, scored 52.2), and at rest, though continuous on the clamped pad, it is steep near the corner a diagonal pair or a triple doesn't hold (up to about 200 per pad unit; a Morph X ramp near B's corner with A and D on moved a weight by 0.094 a sample); now the weights interpolate multilinearly between the on-sets while layers ramp, and at rest keep D1's b_L / s except for a diagonal pair or a triple whose on corners sum to less than ε = 0.35, where each takes an equal share of the shortfall (binding on 17.1% and 7.1% of the pad; never for a lone layer, an adjacent pair or four layers, whose resting weights stay D1's bit for bit; `5693fe9`, the layers spec's §6.3, **Amended**), so the largest weight step a sample is 0.0073 for a resting pad crossing at full speed in both axes, 0.0062 for a handover with the pad still and 0.0105 for both at once at 48 kHz (0.0079, 0.0068 and 0.0116 at 44.1 kHz; ε = 0.35 by review fix round 2's ruling, since 0.25 lets the pad alone pass 0.01 at 44.1 kHz and a larger ε moves D1's resting weights on more of the pad), and at the center a ramp's crossfade is linear in the ramp where the first law's was hyperbolic, since weighting the sets by size to keep that shape would double the worst handover step to 0.0124; the first fix, `eae7a36`, which shared every off corner's weight equally and so moved the resting weights of every pair and triple over most of the pad, was replaced after review; 51 of the 53 golden WAVs keep their SHA-256, and fx-layer-switch and fx-overload move inside their Layer On ramps only, where the crossfade at the center is now linear rather than hyperbolic (at most −24.2 dBFS); row 301 is closed (`5693fe9`). **D1-rest, option 1** (the director's decision of 2026-10-31): D1's denominator is floored near the corners no rendering layer holds, as row 301's fix built it (ε = 0.35). The resting weights stay bit-identical to D1's for adjacent pairs and for all four layers everywhere, and for diagonal pairs and triples wherever the floor doesn't bind. The floor binds on 17.1% of the pad for diagonal pairs and 7.1% for triples, near the corners no rendering layer holds, where the on layers share the shortfall equally. Inside a Layer On ramp the center crossfade is linear in the ramp. The layers tests pin both: the bit-identity over all 11 on-sets, and, at named pad points where the floor binds, D1's first value beside the new one (declined: keeping D1's resting law unfloored). **The integration review's I2** (2026-11-01; the director's D-specsave, 2026-11-01, option 1): under D2 (Freeze on a Spectral layer freezes its live ring exactly as on a Live layer) and sub-project 2's D1 (a held ring is saved when grains read it), a held ring is saved for a Spectral layer whose material is its ring (no sample loaded), and restored and armed as a Live layer's is; a Spectral layer with a sample saves no ring, as a Sample layer doesn't (`gg::core::ringSaved`, given the layer's material). The director's D-specwindow (2026-11-01, option 1): a Spectral layer's analysis follows sub-project 2's Loop, Loop Length and Splice window and the ring commands reach it; deferred at first, built by the M2 close (row 381), which also brought DC and Nyquist to unity (row 365) and added the reverb's onset fade to §14 (row 302). **The director's D-corners** (2026-11-01, option 1): the extremes corners' boundaries stay inside the Reverb Size exemption; carry-over row 382 keeps the finding on record. *Decided 2026-10-30 to 2026-11-01 by the director (D1–D8 and D7a, 2026-10-30; D-bound, D5b, D-glide, D1-rest and row 301's assignment, 2026-10-31; D-specsave, D-specwindow and D-corners, 2026-11-01), the program manager (relaying them) and Claude (the rulings made during the build, the merges and the reviews), during M2 sub-project 4; this line added by the M2 close's consistency pass (2026-11-02, #89).* |
| 70 | M2 sub-project 5 (Presets and content), groups 1 and 2: the `.ggpreset` file, INIT, the discovery provider, the preset strip and browser, Mutate, the pinned CC0 content and `gg-render`'s preset modes | **What group 1 built** (2026-10-31; `docs/superpowers/specs/2026-10-30-m2-presets-design.md`, Tasks 1 to 13 and 10b, PR #17): the preset file (a `GGST` container with a `<preset>` element in `PTCH`), INIT, the CLAP preset-discovery provider and the catalog, the strip and browser in both editors with favorites and "Save as…", Mutate, the pinned factory content (`giga-grains-content` release `v1`, fetched by `scripts/fetch-content.sh`, cached in CI, staged by `gg-products`), and `gg-render`'s `--preset`, `--pack-preset` and `--content-dir`; every golden case's WAV hash is unchanged (36 before main's merge, all 43 equal to main's after), and group 2 (Tasks 14 to 18, the 48 voiced presets and content v2) goes in a new branch and pull request. **The director's decisions** of 2026-10-30, by forced choice on the spec (§8 there): **D1** to **D9**, option 1 of each (the preset count and spread, the CC0 sources, what Mutate touches by default, Mutate's law, a sample-less preset keeping the loaded sample, a load keeping the instance's seed, the MIDI-driven FX presets' rest, INIT as the product's defaults, a browser listing only its own product's presets). Of 2026-10-30, late: **a saved state keeps the loaded preset's edited mark** (`edited="1"` on `PTCH`; older readers ignore it; no chunk or version change); **Mutate never moves Reverb Decay into ∞ from a finite value**; **content v2, in group 2, adds strings and brass from VSCO-2 CE (CC0) with full provenance**, and v1 has no voices and no orchestral strings (its Strings are folk instruments) until then; the director took the browser screenshots. **The program manager's rulings:** CI's Windows leg excludes clap-validator's `preset-discovery-crawl`, `preset-discovery-descriptor-consistency` and `preset-discovery-load`, which run their own path check (carry-over row 233); carry-over row 234 is the Rosetta transient faults row, owned by sub-project 7; group 1 merges before wave 2, and group 2 goes in a new branch. **The worker's engineering rulings** that changed the spec, amended in place there (its §6.2, §6.3, §6.4, §6.5, §6.8, §6.9, §6.11, §10 and §12): one INIT preset carrying both plugin ids; the provider creates and always declares the user presets folder; favorites live in `favorites.txt`, one key per line; the catalog caps a folder walk at 5,000 files (recursive, to depth 8, as clap-wrapper's crawl) instead of using a worker; the Mutate button is in the browser, and the header strip keeps its 90 px slot (sub-project 6's header); Reset locks restores the structural locks only; Mutate's RNG key is layer and stream `0xFFFFFFFF` with `Field::PositionJitter`, no new `rng::Field`; Mutate's values reach the host as gestures in batches of at most 100 slots behind a drain witness, dropped by a load's epoch, with `sendValues` exposed for sub-project 6's whole-state undo; a FILE preset location is a native OS path on every platform. **Part II as edited:** §15 (factory content, clap-wrapper's preset bridges as pinned), §17 (the strip, browser and Mutate as built; the header's layout line), §18 (Presets: the file, what a load keeps, INIT, discovery, content roots, the user folder and favorites, the `NN` prefix of factory file names, the selector as pinned) and §19 (`gg-render`'s preset modes, the preset tests), with `CLAUDE.md` and `docs/m2-carry-over.md` rows 233 and 234. *Decided 2026-10-30 by the director (D1 to D9 and the three late decisions) and the program manager (the rulings above), and by Claude (the engineering rulings); written 2026-10-31 by the Claude session that ran sub-project 5.* **Group 2** (2026-11-01; Tasks C2 and 14 to 18, PR #25, branch `m2-presets-g2` from main at `b538442`; the spec status **Built**): **What group 2 built:** content v2 (`giga-grains-content` release `v2`: 51 FLAC files, VSCO 2: Community Edition strings and brass, CC0, with full provenance, beside VCSL; pinned by `content/pin.txt`); content-relative preset samples (`MEDI` version 2 in the preset file, resolved through the content roots; `gg-render` and the packer share the rule); the 48 factory presets, 24 per product (the FX's with 8 MIDI-driven, the instrument's reading every one of v2's 51 samples); the 48 preset goldens, so 108 goldens in all, with a click-score check in place of the injection table; `gg-render --mutate-preset` and the listening pack (the FX presets over a rendered strings phrase, a Mutate ladder); and the themed strip, browser and Mutate panel (carry-over row 313, in part: LoaderStrip stays with views). The 60 earlier goldens' WAV SHA-256s were unchanged through every task. **Rulings** (the ledger's GROUP 2 section): (1) **content v2 is a strict superset of v1**, v1's 37 files byte-identical at the same paths and the `v1` release untouched, so nothing that names a v1 path breaks (the cost: about 33 MB of the 50 MB budget was fixed, 14 new files); (2) **INIT keeps each layer's Ring Length alone** (amended 2026-11-02 by the final review's fix wave: first ruled to keep all five ring parameters as a preset load does, which bent D8, "INIT is a fresh instance, bit for bit"; only Ring Length sizes a ring, so INIT keeps it, and Loop, Loop Length, Overdub and Splice return to their defaults; the controller's ruling, ratified by the director as D8a on 2026-11-02, #80); (3) **a preset load keeps each layer's five ring parameters** (Ring Length, Loop, Loop Length, Overdub, Splice) as it keeps the seed, a preset file's own values are ignored on load, `gg-render --preset` does the same, and **the packer refuses a factory source that sets one**; (4) **Mutate's structural locks are 62 slots,** by section: wave 2's Quality, Budget, the five ring parameters per layer, Delay Amount and the limiter's Mode, Ceiling and Release are locked; Smear, Bus Split, the buses and the Delay's other four parameters are free, and the panel shows Ring Length as not mutable; (5) **`brass` joins the tag vocabulary** (29 words; the director may strike it at the gate, spec §6.7); (6) **row 313's presets part** is closed: the strip, browser and Mutate panel take the editor's palette and follow a theme switch, 4.5:1 checked as the editor-frame tests do (the dimmed Mutate rows 3:1, the favorite star 3:1 as a glyph); (7) **a content reference never leaves its root:** empty, backslash, NUL, non-UTF-8, absolute and `..` references are refused by one function (`isContentRelativePath`) in the plugins' search and cache, `gg-render` and the packer, after the Task 14 review found the plugins' search step resolving them. **Also recorded:** VSCO 2: CE's readme asks for credit and that the samples not be sold directly; neither binds under CC0, `PROVENANCE.md` credits it and §15 records the request; files are named by measured sounding pitch; a relink drops the content reference; `gg-render` refuses a preset entry that has only an absolute path; the deferred minors are in `docs/m2-carry-over.md` (rows 389 to 401, and 402 at the merge with views' PR 2); the listening gate's items are the noise floor of the quiet sources gained up to +41.8 dB, the hiss of the heavily gained samples under granulation (15 Kalimba Steps in particular), Shard Storm's ticks on drum transients and the dark browser's list ground. **Decided by:** the director (content v2, by forced choice, 2026-10-30, and the sub-project's D1 to D9 above), the program manager (the ring rulings, row 313's presets part, and that this row is extended and not a new one), and Claude (the rest). **Part II as edited:** §15 (release v2 and its credit request), §17 (the Mutate locks, the themed panels, the status line), §18 (what a load keeps, INIT, content references, the factory presets) and §19 (the preset goldens, `--mutate-preset`, the content-relative samples), with `CLAUDE.md`, `docs/m2-carry-over.md` and the spec. *Group 2 written 2026-11-01 by the Claude session that ran it.* |
| 71 | M2 sub-project 6 (Views), PRs 1 and 2: the four views, four lanes with a pitch axis, the layer strip and the XY pad, the Mod view and the route path, undo and redo, themes and the palette, the About box, the GPU spike | **The director's twelve decisions** (2026-10-30, by forced choice on the spec and, for D12, on the final review, relayed by the program manager; `docs/superpowers/specs/2026-10-30-m2-views-design.md` §8), option 1 each but D12's option 2: **D1** the lane's vertical axis is pitch, ±48 semitones across the lane, and pan is no longer drawn; **D2** the scan window is a rectangle, its left and right edges Spray, its top and bottom edges Pitch Spread and its body Position or Pitch, locked to the axis the first 4 px pick, which amends §17's "drag vertically for pitch spread"; **D3** four lanes always, an off layer's dimmed; **D4** Okabe–Ito's palette, A orange `#E69F00`, B sky blue `#56B4E9`, C bluish green `#009E73`, D reddish purple `#CC79A7`, the letter beside the color everywhere; **D5** the Edit view shows one layer at a time; **D6** the Mod view is the Edit panels under a strip of source chips, with the route list beside them; **D7** undo covers the editor's own edits, 200 deep, kept while the instance lives, plus whole-state entries for sub-project 5's Mutate and preset recall; **D8** dark by default, light by the header's switch, kept per machine; **D9** the lanes stay dark in both themes; **D10** Reverb Size sets on release in the FX view (row 217); **D11** the window opens at 960 × 600; **D12** (from the final review) the canvas repaints at the display's refresh rate, with no cap, option 2 of three (a 60 Hz cap and a cap only above 120 Hz were declined), its cost about 65% of the message thread on a 240 Hz display under load per the spike (contended) and an estimated 35% at 120 Hz idle, each open editor adding its own share (§16). **The program manager's acceptance** (2026-10-30): the FX view's Reverb section is PR 1's, and this row takes the next free number at merge, after sub-projects 3, 2, 4 and 5. **The design** (the spec and `docs/superpowers/plans/2026-10-30-m2-views.md`, as built in PR 1; §17): `UiSnapshot`'s per-lane fields and the morph (`5c15566`); the route path from the editor (`c40f585`); the license set in one list, embedded into the editor (`2ad02b2`); the theme as a per-machine setting (`3a4df76`, `007a8fe`); the palette, the layout table and `ParamControl` (`d84081c`, `9044019`); the lanes' math in `gg-ui-core`: the pitch axis, the scan rectangle and its hit test, the axis lock, the pad's law and the banded resolve (`fbfcb0a`, `228e97f`); `gg-bench --canvas` timing the four-lane banded resolve (`c729f02`); the undo stack (`4ba69ca`, `9296b48`); the layer strip and the pad (`e691a1e`, `e0b3de4`); the About box (`2116004`); the Edit view (`462495d`, `b6f1842`); four lanes on the canvas (`75c412f`, `946e283`, `ce7974d`); the FX view (`0d8969c`); the Mod view (`7df6ca0`, `688f9bd`); the GPU spike (`2291278`); the editor's frame, with the four views, undo and redo, the themes, the keyboard and the About box in one editor (`b07f25a`, `50c9e0c`); and `gg-snap`, CI's `gg-snapshots` artifact and the listening pack, `docs/listening/m2-views/` (16 snapshots, three render specs and the host checklist), made for the record and seen and heard at M2's close (`0cc3cf7`); and the fixes the snapshots called for: knob labels without their panel's title, wrapped to two lines, the strip's labels, its column following the theme as chrome, and the route list's colors and scroll to a new route (`fae408d`). No parameter was added and no DSP changed; every one of the 36 goldens kept its WAV SHA-256 at every check made, among them the wave-1 gate at `228e97f`, Tasks 1, 4 and 6 (`5c15566`, `c40f585`, `3a4df76`) and Task 15 (`b07f25a`, `50c9e0c`), and at the PR's head `25c7707` (the final gate: all 36 equal the baseline at `172af2b`). **The canvas figure** (§16): `gg-bench --canvas` idle at 8,192 grains with four lanes, 2.959 ms (mean 3.117, p95 4.009), at or under the 3 ms budget by its median; the live readout, report-only, 3.50 ms median (`docs/benchmarks/2026-10-30-m2-views-canvas.json`; §16). **The spec's calls that amend Part II** (its §9): a lane draws its layer by the layer's built kind, in either product; one canvas with one accumulator, its resolve tinted per lane; the layer strip is the lanes' header column; the theme is a per-machine setting both products read, never in the state; the editor sets routes through a route path of its own (VW-T4a, below); Reverb Size sets on release in the UI, with automation unchanged. **The GPU spike** (`docs/spikes/m2-gpu-canvas.md`; the code on `spike/gpu-canvas` at `082a874`, unmerged): JUCE OpenGL and Metal were built and measured within the timebox; Visage was not tried. Interleaved, five runs a path, at 8,192 stress grains on a canvas of 1600 × 600 logical at 2×, under contention (load average 10.9 to 18.6): the CPU path's readout 4.10 ms, OpenGL 0.20 ms on its GL thread plus 0.38 ms of GPU, Metal 0.09 ms on the UI thread plus 0.45 ms of GPU, so the candidates win by 6.6× to 45× depending on the column; neither clears "no new platform risk", so the CPU renderer stays (§17, §22). **Rulings made during the build** (IDs: VW, Views, then the task; the spec's §16 lists them, each marked **Amended** in place): VW-T2a, a corner of the scan rectangle takes the Spray edge, and on a Live lane the right corners take the top and bottom edges, since the right side is the body (PU-T8a); VW-T2b, the body keeps a height to grab, `max(tol, H/2 − tol)` either side of its center, the top and bottom edges grabbing only beyond it, so a rectangle at Pitch Spread 0 has a body; VW-T2c, a reverse grain draws centered, as rate 0 does; VW-T3a, macros, modulators and MIDI take three of the four hues Okabe–Ito leaves after the layers', yellow (an ochre on the light ground, where the yellow is about 1.2:1), vermilion and blue, and since the fourth, black, can't be seen on the dark ground, the follower is gray and the transport the text color; VW-T3b, the GUI tests run on every leg built with the editor, as M1's do (the spec's macOS-only premise was wrong), and only `snap_cli` is macOS-only; VW-T3c, a layer's Level is placed once, in its Space panel, and the strip shows On, Level, Source and Freeze as second controls with the same IDs; VW-T4a, the routes take a `LatestValue<ModRoutes>` channel of their own, taken after the loaded state, since one channel would let an edit overwrite a pending load; a load while active also feeds it and a deactivation drops an untaken edit, so the later write wins in either order (§18); VW-T5a, the undo stack lives in `gg-ui-core`, framework-free and sanitizer-tested, and `EditorPlugin` owns the instance, declared to outlive every editor; VW-T6a, the settings file's NUL guard covers `theme`, one addition to sub-project 2's `media/src/SettingsFile.cpp`; VW-T9a, the canvas keeps 8,192 grains per layer, not across the four, so a dense layer can't push another's out; VW-T9b, the lanes are half-open row ranges, a shared row the lower lane's; VW-T9c, a press in a lane selects its layer, and a body press that never moves 4 px is no gesture; VW-T10a, the pad's drag is one undo entry, "Morph", and a real double-click two, its first press an ordinary click (a checklist item); VW-T11a, rings are drawn on knobs only, since the Step sliders and every modulator's control are no target, and the spread band takes Position and Spray as their 0–1 values; VW-T11b, mod mode's inertness covers the accessibility value interface; VW-T12a, toggles and menus show one dot per route in its source family's color, their depth edited in the route list; VW-T12b, a route to all layers counts as that source's route on every layer, so a drop selects it rather than doubling it; VW-T14a, the About box's format comes at run time from clap-wrapper's `clap-wrapper.host-information/0` extension, since every format wraps the same plugin code; VW-T15a, notices reach the header through the component tree (`NoticeSink`); VW-T15b, the view chosen is remembered per plugin instance, unsaved; VW-T15c, the About box is modal, the keys and Tab its own while it shows; VW-T15d, undo and redo do nothing while a gesture or group is open, the gesture's control alone able to end it consistently with the host; VW-T15e, a parameter entry's `before` is the control's shown value at its gesture's begin, since `base` lags the audio thread just after a gesture; VW-T15f, while undo or redo applies, the stack ignores what its callbacks ask of it; VW-T15g, in the Perform view only the layer strip's controls and the macro row take a source chip, reached by switching views mid-drag (⌘1), and the pad takes none, so Morph X and Y are routed from the route list; VW-T17a, the spike's decision rests on the ratios and the risk clause, not on absolute figures, so it is not provisional on the idle run; VW-T17b, decided (D12): the canvas repaints on every VBlank it can make, about 156 times a second on the spike's 240 Hz display, about 65% of the message thread at the contended 4.1 ms; §16's budget is per frame and is not affected, and the director chose the display's rate over a cap on 2026-10-30. **The carry-over:** rows 217 (`0d8969c`) and 221 (`2ad02b2`, `2116004`) closed, and `THIRD_PARTY_NOTICES.md` now says the About box shows the texts; rows 85 and 232 name `plugin/src/editor/EditorPlugin.h` and `.cpp`, which this sub-project edited, and stay open, waiting on upstream; the director retired the carry-over's count rules on 2026-10-30 (INTENT §23 #67). **PR 2** (built 2026-11-01; the plan `docs/superpowers/plans/2026-11-01-m2-views-pr2.md`, the spec's §16 second table; §17, §18, §19): the Ring, Delay and Limiter panels and the Filter's bus lines on one panel grid (`43fa36b`); the plugin's per-layer sample, ring, capture and memory API (`30e0114`); the ring's lanes, the lane menu and drops on every lane (`988529d`, `4a4a55f`); the sample strip for every layer in both products, the memory readout and cap, Collect (`d4a2b8e`, `4a03997`); whole-state undo in batches and merged key repeats (`8e077ad`); the test split (`dc45c6b`); and the 16 snapshots remade with a layer-B sample and the FX's ring marks. No parameter and no DSP changed; every one of the 60 goldens kept its WAV SHA-256 at each check of a task that touched `core/` or `plugin/`. **PR 2's rulings** (2026-11-01, the plan's and the controller's; the spec's §16 lists each, marked **Amended** in place): VW-P2a, the ring's operations are a lane menu (a right-click, or Shift+F10 on the keyboard lane), not a drag vocabulary, and there is no record command, since the ring records whenever it isn't frozen (record is Freeze; Loop and Overdub are parameters); VW-P2b, splice marks and captures are no undo entries, Loop from the menu is one; VW-P2c, a file dropped on any lane loads into that lane's layer in both products, and on a Live layer leaves its Source alone and says so, as does a capture into a Live layer (amended after the final review; a capture into a Sample layer says "Captured into <L>"); VW-P2d, amended, a Live lane spans `ringFrames / sampleRate` (the ring's storage), else 16 s, since `LivePeaks::frames()` is always 16 s, and the peaks draw only over the strip's frames; VW-P2e, the FX view follows the signal chain, Delay, Reverb, Limiter, Output, and Edit gains a Ring panel after Source and a Filter panel with each bus on its own line; VW-P2f, the sample strip follows the selected layer in both products, in Perform and (compact) in Edit, with the memory readout, the cap menu and Collect, and the cap is a setting with no undo entry; VW-P2g, on a Live lane the scan rectangle, its ghost and its body drag reach Position up to the span (`livePositionMax`, at most 300 s); VW-P2h, a lane-menu mark takes the engine's anchor while the lane's outline follows `MediaSession::spanOf`, which disagree with Loop on and a mark older than the loop (row 386); VW-P2i, the menu's Loop stays enabled while inactive, its other items not; VW-P2j, the layer strip's Source button (the Mod strip's is the same strip) shows a short form ("Resamp", "Spec", "Osc"; "Sample" and "Live" keep their names) where the full text doesn't fit, the full names kept in the popup, tooltip and accessibility value, while Freeze never shortens and takes width from On (Source takes it from Level), so every strip control reads at 640 × 360; VW-P2k, below 900 px the strip folds Sample folder and Collect into a "..." menu and a refused drop's notice goes to the header when the strip is hidden; VW-P2l, the Live-drop notice says the sample "is loading", since a drop only requests the load; VW-P2m, the sample strip shows in both products; VW-P2n, the chain order was checked against `Processor.cpp`. **The carry-over (PR 2):** rows 312, 314, 315, 316, 318 to 329, 332 and 334 to 338 closed (338 at `293cbb5`), 313 and 330 in part, 331 (the header's split, waiting on sub-project 5's group 2) and 333 open, and rows 383 to 388 added. Decided 2026-10-30 by the director (D1–D11) and the program manager (the acceptance), and 2026-10-30 and 2026-11-01 by Claude (the rulings), during M2 sub-project 6; *written by the Claude session that ran sub-project 6*;  see §14, §16, §17, §18, §19, §22 |
| 72 | Full theme support goes in M5 | **The director's decision of 2026-11-01,** made during M2 wave 3 after asking whether the editor has comprehensive theme support. It has two fixed themes (M2 sub-project 6, D8 and D9: dark by default, light by the header's switch, the lanes dark in both; `Palette::dark()` and `Palette::light()`, sst-jucegui's own dark and light style sheets). M5 adds all four of: **(1)** a System choice beside Dark and Light that follows the OS appearance live on macOS, Windows and Linux; **(2)** a built-in high-contrast theme for low vision (text at 7:1, heavier outlines and focus rings), checked by tests as the light theme's 4.5:1 is; **(3)** more built-in themes; **(4)** user theme files, loaded from a user folder and shareable, covering the editor's named colors and sst-jucegui's style keys. In M5 rather than M3 or M6 so the theme system is settled before the first public beta (#62) and users never see it change under them. The theme file's format is versioned; once public, users' files depend on it, so M5's spec states its compatibility rule. The layer palette stays Okabe–Ito's four (D4) unless M5's spec rules otherwise for high contrast; whether the lanes may leave dark is M5's spec's question (D9 keeps them dark). *Decided 2026-11-01 by the director; written by the program manager.* |
| 73 | Pushes to `main` run the quick benchmark | **The director's decision of 2026-11-02,** asked during M2 wave 3: "can we disable the full benchmark tests on main?" A push to `main` now runs `gg-bench --quick` on every build leg, as a pull request does (#57): a smoke check with no baseline, no verdict and no artifact, a crash or exit 1 or 2 still failing the job. The full-length run (about 6 minutes a leg before the later cases, more since) stays on the nightly schedule, which measures `main` once a day, on `workflow_dispatch`, which is how a baseline's three runs are made on demand, and on a pushed `v*` tag, so a release keeps its record. This amends #57's "the full-length run happens on pushes to `main`". **The full run's limits** (the director's choice the same day, of three: raise the limits, make the nightly quick too, or leave the nightly red for the close): since M2's sub-projects 2 and 4 added their cases, every leg's full run outgrew the benchmark step's 30 minutes (the nightly of 2026-11-02 finished warm-up and round 1 of 5 in 30; `main` was red from 2026-11-01), so the step's limit is 60 minutes and the build job's 120, not 90; shortening the run (`engine-live-long`'s 300 s warm-ups, among others) is sub-project 7's to weigh. Nothing else in CI changes, and the caches are still saved by pushes to `main` (#57; CLAUDE.md, "CI caches"). *Decided 2026-11-02 by the director; written by the program manager.* |
| 74 | CI's sanitizer tests in seven jobs | **The director's decision of 2026-11-02,** asked during M2 wave 3 ("sanitizers takes almost an hour to run and that seems incredibly long"), of four options (shard only, shard and run ASan's `slow` tests only on the nightly, shard and move Windows to Ninja, not now): **shard, and the slow tests on the nightly.** One job ran ASan + UBSan, then RTSan, for 50 to 66 minutes; ASan's 1,369 tests took 43 of them, about 19,700 s of test time on one 4-core runner, while ccache served the build in seconds. ASan now runs in six shards, side by side, each taking every sixth test (`ctest -I <shard>,,6`), and RTSan in its own job; one check, `sanitizers (asan, rtsan)`, passes only when all seven do, so the ruleset's required check and the release job's dependency keep their name. Six, not four, because the stride's balance was measured against that night's per-test times (four shards put 7,450 s in one). The repository is public, so the extra jobs cost no minutes. On a pull request and a push to `main` ASan leaves out the tests labeled `slow`, which the nightly, a dispatch and a tag still run; RTSan runs them all everywhere, and every `slow` test still runs without a sanitizer on every build leg's ctest. Closes carry-over row 339. *Decided 2026-11-02 by the director; written by the program manager.* |
| 75 | CI's build legs: the Rosetta slice three at a time, a one-round benchmark smoke | **The director's decision of 2026-11-02,** asked right after #74 ("are there ways to speed up the build legs?"), choosing three of four offers (Windows on Ninja with a compiler cache was declined for now). PR #27's run took about 31 minutes on macOS, 28 on Windows and 12 on Linux. **(1)** The macOS leg's x86_64 slice under Rosetta ran `gg-tests` as one process, then the golden cases one at a time: 16.2 minutes against its 20-minute limit. It now runs three disjoint Catch2 shards at once (`--order decl`), then the `[serial]` cases alone (they measure time or memory), then the golden cases three `gg-render` processes at a time; no case is dropped. **(2)** On a pull request and a push to `main` that step leaves out `[slow]`, as ASan does (#74); the nightly, a dispatch and a tag run it all, and the native arm64 ctest always does. **(3)** The quick benchmark on a pull request and a push to `main` (#57, #73) runs one round, not five (`--quick --rounds 1`), about 1 minute instead of 4 on every leg; it was a smoke check with no verdict already, and the full runs are unchanged. *Decided 2026-11-02 by the director; written by the program manager.* |
| 76 | CI's Windows leg on Ninja with a compiler cache | **The director's decision of 2026-11-02,** reversing that day's "not now" on the fourth offer of #75 before sub-project 7 began. The Windows leg built with the Visual Studio generator and the ClangCL toolset, which takes no compiler launcher, so it recompiled everything on every run: 15.4 of its ~28 minutes on PR #27's run. The `windows-clangcl` preset is now **Ninja with `clang-cl`, Release**, single-configuration, still into `build/windows/`. CI loads vcvars64's environment and puts Visual Studio's own LLVM (`VC\Tools\Llvm\x64\bin`, the compiler the ClangCL toolset used) first on the PATH, ahead of the image's separate LLVM, and names MSVC's `link.exe` as the linker, as the toolset used, since Git Bash's `/usr/bin/link` would otherwise come first; so the compiler, linker and flags are meant to be unchanged, and the goldens' event logs, compared exactly, check it. **ccache 4.14.2** for Windows (ccache's release, its SHA-256 checked) caches the leg as on macOS and Linux, under the same key scheme. A local Windows build now runs from an x64 developer shell. Steinberg's validator still builds with the Visual Studio generator. **Amended the same day:** the CLAP, the VST3 and the instrument's standalone all link as "Giga Grains" from `clients/clap-first/`, and MSVC's linker writes each one's `.exp` and `.lib` beside the others; Ninja linked two at once on a dispatch run (LNK1104, run 41543261855), which the Visual Studio generator never did, so on Windows with Ninja those four targets link one at a time (a job pool of one) and everything else stays parallel. Part II's §15 toolchain line is edited. *Decided 2026-11-02 by the director; written by the program manager.* |
| 77 | Documents-only pull requests skip the build and the sanitizers | **The director's decision of 2026-11-02,** asked when the close's Phase A pull request ran full CI over documents alone ("I don't think doc changes should require a full CI run"), of three options (skip with one required gate; skip by step, keeping the five named checks; leave CI as it is): **skip, with one required gate.** `plan` reads a pull request's changed files (its merge commit against the base) and calls it documents only when every file is under `docs/`, or is `INTENT.md` or `CLAUDE.md`; the build legs and the sanitizer jobs then skip. Every other Markdown file is an input to the build or a test (`THIRD_PARTY_NOTICES.md`, the content's and installers' READMEs and `PROVENANCE.md`, the root `README.md`), so it counts as code. A skipped matrix job never reports its per-leg check names, so `main`'s ruleset now requires **`ci-ok`**, a job that passes only when `plan` passed and every build leg and sanitizer job passed or, documents only, was skipped, beside `clang-format`, in place of the five named checks (#57's three legs, `sanitizers (asan, rtsan)`, `clang-format`). Pushes, the nightly, dispatches and tags never skip. *Decided 2026-11-02 by the director; written by the program manager.* |
| 78 | The restore criterion is what was built | **The director's decision of 2026-11-02** on the consistency review, `docs/m2-consistency-review.md`'s K-1, option 1 of two: **every restore renders identically and restores the saved frames bit for bit; the clock and the grain index restart**, so a restored session is not the saved session continuing (ruling SP-T7e, as built). It amends #64's criterion, that every restore "matches the saved session within the goldens' tolerances", which nothing tested: the float32 `AUDI` payloads (#68) restore a ring's frames bit for bit, and the `[plugin]` tests gate that every restore renders the same. The cost: a reopened frozen pad replays the same material with new grain draws, never the saved session's continuation. Declined: keeping the criterion and adding a check against the saved session continuing, which a sparse cloud may fail and which, met exactly, needs the clock and grain index in the state, a format change. §18 (Deterministic, not continuous; `AUDI`) says so, and #64 and #68 point here; the sources spec (goal 1, §6.6, §10) and the program design's §7.5 follow it. *Decided 2026-11-02 by the director (forced choice, option 1; the close's spec §8.1); written by the M2 close's documentation task (Claude).* |
| 79 | A click entering Peak mid-sound is part of Peak's law | **The director's decision of 2026-11-02** on the consistency review, `docs/m2-consistency-review.md`'s K-2, option 1 of two: **the click on entering Peak mid-sound is part of Peak's zero-attack law (D5b, #69)**. Inside the 10 ms mode crossfade over hot material Peak's gain leaves 1 with no attack where the input first passes the ceiling: 24.4 at 461 samples into the fade at −18 dB (carry-over row 378, closed), in the call before the first one whose gain passes −6 dB, so outside D5b's "nothing wider", and (before K-2) exempt only when the draw's calls fell so that the dip landed in an earlier call. The sweep harness exempts the mode fade by samples, not by where its calls fall. It relaxes for Peak the extremes spec's anti-goal "no click when the limiter's mode changes" (§3 and §6.5 there). Declined: a defect, Peak's gain inside a mode crossfade falling no faster than the fade (a limiter DSP change; `fx-limiter` and the presets that use Peak re-checked by ear). §14 (The global limiter) says so; #69 points here. *Decided 2026-11-02 by the director (forced choice, option 1; the close's spec §8.1); written by the M2 close's documentation task (Claude).* |
| 80 | D8a: INIT keeps each layer's Ring Length | **The director's decision of 2026-11-02** on the consistency review, `docs/m2-consistency-review.md`'s K-3, option 1 of two: **D8a, INIT keeps each layer's Ring Length** (memory, not sound), ratifying the controller's ruling of 2026-11-02 in sub-project 5's final fix wave (#70's ruling (2)), which bent D8 ("a fresh instance, bit for bit") with no sign-off recorded; Loop, Loop Length, Overdub and Splice return to their defaults. The cost: INIT differs from a fresh instance whenever a ring isn't 16 s. Declined: INIT resetting Ring Length to 16 s, which resizes every ring on the main thread, can be refused by the memory cap and cuts a held ring's audio past 16 s. §18 (INIT) says so and #70 points here; the presets spec's D8 and carry-over row 391 name the decider. *Decided 2026-11-02 by the director (forced choice, option 1; the close's spec §8.1); written by the M2 close's documentation task (Claude).* |
| 81 | The overload ladder's order is by load | **The director's decision of 2026-11-02** on the consistency review, `docs/m2-consistency-review.md`'s K-4, option 1 of two: **Part I §8.4's order (a lower interpolation tier first, then dropped new grains, then stolen grains with fades) is the order of the rungs by load, and D4 (#66) decides between dropping and stealing**: lower the tier; while the oldest sounding grain is under half its window, drop the new grain; past it, steal it with a fade. That is how the engine spec read D4 and how the code builds it; a reader of the per-grain tests, which steal the oldest grain past half its window before they drop, sees the steal first. **No Part I edit:** §14's list and its ladder (Hard engineering rules) gain the clause, and so does CLAUDE.md's non-negotiable. Declined: amending Part I §8.4 to the tests' order ("lower the tier, then steal the oldest grain past half its window, else drop"). *Decided 2026-11-02 by the director (forced choice, option 1; the close's spec §8.1); written by the M2 close's documentation task (Claude).* |
| 82 | A session saves held rings only where grains read them | **The director's decision of 2026-11-02** on the consistency review, `docs/m2-consistency-review.md`'s K-5, option 1 of two: **only a layer whose base Source builds to Live saves its held ring** (Live or Resample, and by D-specsave, #69, a Spectral layer with no sample; `gg::core::ringSaved`), ratifying the controller's narrowing (sub-project 2's final review, 2026-10-31, #68) of the director's D1, "a ring is saved when it is held". The cost: a frozen Sample layer's ring, which still records (SP-T5c), is lost on reopen, so switching that layer to Live then gives an empty ring. Declined: saving every held ring, Sample layers included (6.1 MB a layer at 16 s, up to 115 MB at 300 s, of audio no grain reads unless the kind changes; the 32 MiB embed cap fills sooner). §18 (What a session saves) says so and #68 points here. *Decided 2026-11-02 by the director (forced choice, option 1; the close's spec §8.1); written by the M2 close's documentation task (Claude).* |
| 83 | Part I §5: live buffers are saved when held | **The director's Part I sign-off of 2026-11-02**, for one clause of §5 alone, on the consistency review, `docs/m2-consistency-review.md`'s K-6, option 1 of two: "minutes-long *live* buffers inside a plugin, saved with the session and managed with splices" now reads **"saved with the session when held (frozen or looped) and managed with splices"**, as D1 decided (2026-10-30, #68) on Part II, which had left Part I promising every live buffer saved. Nothing else in Part I changes. Declined: keeping Part I and saying in §18 that §5 means the held case, which leaves Part I over-promising to a reader of Part I alone. *Decided 2026-11-02 by the director (forced choice, option 1; the close's spec §8.1); written by the M2 close's documentation task (Claude).* |
| 84 | A sub-project rebases on, or merges, the merged result | **The director's decision of 2026-11-02** on the consistency review, `docs/m2-consistency-review.md`'s K-7, option 1 of two: **a sub-project rebases on, or merges, the merged result, then re-verifies**, ratifying the program manager's mechanism (#68, #69: "a merge, not a rebase"), which replaced #64 (4)'s "each rebased and re-verified". Every M2 merge was re-verified, and M2's history stays non-linear. Declined: keeping "rebase" as the rule and recording M2's merges as departures, a rule stating what wasn't done. The program design (§4, §6) and plan (C1) follow it, and #64, #68 and #69 point here; the merge order is #88's. *Decided 2026-11-02 by the director (forced choice, option 1; the close's spec §8.1); written by the M2 close's documentation task (Claude).* |
| 85 | Carry-over rows 118 and 119's move ratified | **The director's decision of 2026-11-02** on the consistency review, `docs/m2-consistency-review.md`'s K-8, option 1 of two: **the move of carry-over rows 118 and 119 from sub-project 2 to sub-project 1 is ratified** (the program manager's acceptance, 2026-10-29, #65), at no cost, the work built and reviewed (`263696d`); **the rule stands** that such a move is a design amendment needing the director's word as well as the program manager's (the program plan's A3). Declined: amending A3 so the program manager alone may move an item that shrinks a later sub-project, which would hide scope moves from the director. #65 points here; the program design's I9, §6.2 and §8 follow it (the review's R-53). *Decided 2026-11-02 by the director (forced choice, option 1; the close's spec §8.1); written by the M2 close's documentation task (Claude).* |
| 86 | The close's DIRECTOR rows, the gate's shape and its host checks | **The director's answers of 2026-11-02** (forced choice, option 1 of each; the close's spec §8.2 to §8.4, `docs/superpowers/specs/2026-11-02-m2-close-design.md`). **The carry-over's five DIRECTOR rows:** **row 402**, an undo of a preset recall restores the values and routes; the recalled samples and name stay, recorded as decided in §17 (declined: restoring the name too, M; the name and the samples, L, an undo re-decoding files or holding old samples against the memory cap); **row 333**, the Mod view's route list auto-selects an added route only when exactly one route appeared (an undo, a redo, a drop), never on a load, built in the close (declined: any added route, as built; never); **row 393**, VSCO 2: CE's request for credit and that its samples not be sold directly, which CC0 doesn't bind, is closed as recorded (§15, `PROVENANCE.md`, `THIRD_PARTY_NOTICES.md`; declined: open until a decision about selling); **row 396**, the preset names and tags stay as they are (all 24 FX presets tagged `live-input`, "Horizon" in both products' names), and the director may strike words at the gate (declined: striking `live-input` and renaming one "Horizon" now, which moves the affected preset goldens); **row 387**, the views' eight new user-visible strings are approved or edited at the gate, as checklist items (declined: holding them for a full text pass before M5). **The gate's shape** (§8.3): one README and one numbered checklist, in sections by pack (layers, engine, sources, extremes, presets, views), then the carry-over's gate rows, then the host checks, each item answerable in a word (yes, no or a pick) and an unanswered item standing as "accept": one sitting of about two hours (declined: six per-pack checklists and a host-checks file; two sittings, ears then hosts). **The host checks** (§8.4; the program design's §7.11): a focused matrix of eight cells, Live 12 with the VST3 and the AU and Bitwig with the CLAP and the VST3, both products in each, each cell one short script: the scan and load; the factory presets in the host's list and one loaded; the editor in both themes and resized from 960 × 600 to 640 × 360; one parameter automated; the project saved and reopened with a frozen ring; notes into the instrument and audio through the FX; the frame-time readout at `GG_STRESS_GRAINS=8192` (the views' exit); whether the host's thread pool is seen (§22's survey). About 45 minutes; the standalone apps are checked by the agents' tests alone (declined: twelve cells with both standalone apps, about 90 minutes; one smoke per host, about 20 minutes, which leaves the presets spec's four-format exit unmet). *Decided 2026-11-02 by the director (forced choice, option 1 of each); written by the M2 close's documentation task (Claude).* |
| 87 | M3 runs as M1 did | **The director's decision of 2026-10-31,** made during M2 wave 3 and recorded until now only in the program manager's notes (the consistency review's R-29): **M3 runs as M1 did, one session, its sub-projects in series, with parallel tasks inside each**; no worker sessions, waves or program manager. M2's agent-run program (#64) is M2's alone. §19 says so. *Decided 2026-10-31 by the director; written 2026-11-02 by the M2 close's documentation task (Claude).* |
| 88 | A wave's sub-projects merge as each is ready | **The director's decision of 2026-10-30** (about 23:45, during M2 wave 2), recorded until now only in the program manager's notes (the consistency review's R-51): **finished branches merge as ready**, so #64 (4)'s fixed order within a wave (3, then 2, then 4; 5, then 6) gave way to readiness, the later sub-project merging the merged result and re-verifying (#84). The order on `main`: sub-project 3 (PR #16, then its follow-up, PR #23), then wave 3's presets group 1 (PR #17) and views PR 1 (PR #18), then sub-projects 2 (PR #19) and 4 (PR #20), then views PR 2 (PR #24) before presets group 2 (PR #25); wave 2's three kept #64's relative order, 3, 2, 4. Presets' two pull requests (#70) were unforeseen in the program design's §6.5. §19 says so; the program design and plan follow it. *Decided 2026-10-30 by the director; written 2026-11-02 by the M2 close's documentation task (Claude).* |
| 89 | The M2 close's consistency pass on INTENT | **The director's request** for the close ("a full consistency pass on the documents", item 1 of M2 sub-project 7) produced `docs/m2-consistency-review.md` (2026-11-02, at `main` `0189dc9`): eight conflicts, answered as #78 to #85, two defects, which are code, and 69 drifts, of which R-1 to R-36 are INTENT's. **Part II as edited** (each fix naming its deciding source): the header's status and §20's M2 row (sub-projects 1 to 6 merged, the close under way; the exit against the committed baselines); §14 (the Source kinds with Spectral; the morph's corners with Y up; the tap's cost, two stores a sample whatever the kinds, SP-T12ac; Ring Length refused as a route's target, SP-T12c; the instance's memory, recomputed from its components' sizes at about 80 MB, not measured; the steal's overshoot, about 87 grains, 2% of the default budget; Size set on release, D10; D-glide's crackle and D5b's measured clicks, row 378, in place of "15 to 18"; D-specwindow; the extremes' parameters and the cap's readout drawn since sub-project 6's PR 2); §15 (sst-basic-blocks and sst's roles; the unpinned rows for M3 and later; the in-house list); §16 (the four-layer cases' baseline entries; `engine-live`'s levels at the idle run; the AVX2 dispatch; `--rounds 1`); §17 (the edited mark over the 283 values but the ring parameters; undo as built; Macro 7 on Resample layers); §18 (Position's range per layer kind; Quality and Budget on no remote page; thirteen remote pages; Ring Length's target rule); §19 (the golden counts in merge order, 60 and 48; the 30 sweep cases; the 283 slots; the PKG's Content component and the CC0 text; who runs M2's sub-projects and M3, #87, #88); §21 (the cap's readout; JUCE 9.0.4, no fallback). The §23 rows are history: they gain pointers (#47, #50, #57, #63, #64, #65, #67, #68, #69, #70), and only factual slips are corrected in place (#48 and #49's stale "pending" tails, #69's "closed as defects" and its missing decided-by line, #70's title, v1's strings and row range, #71's rows). Part I is untouched but §5's one clause (#83). Left for the close's end: §20's M2 row closed, §16's idle figures, the header's "M2 closed" and the close's own row. **Amended 2026-11-03 (the close's final review):** the memory list now names `LivePeaks` (sized for a 300 s ring in every instance, about 0.9 MB a layer at 48 kHz, outside the cap). *Written 2026-11-02 by the M2 close's documentation task (Claude); the decisions are the sources each fix names.* |
| 90 | Dependency upgrades, and the dependency watch | **The director's decision of 2026-11-03,** added to M2's close as its Task 22: upgrade exactly five pins, each to an exact SHA or tag, one commit each with its checks. **dr_libs** to `master`'s head `35b5c39` (dr_wav 0.14.6, dr_flac 0.13.4, dr_mp3 0.7.4; every golden WAV SHA-256 unchanged, 108 compared). **fmt** to **12.2.0**, whose `<fmt/core.h>` no longer brings `fmt::format`: sst-basic-blocks' `ParamMetadata.h` still relies on it at its upstream head, so gg-core, which every user of that header links, carries fmt's own opt-in, `FMT_DEPRECATED_HEAVY_CORE`, as a public definition (`core/CMakeLists.txt`; no submodule edited). **clap-wrapper** to `next`'s head `4585bc6` (thirty commits after v0.17.0): every frozen identity unchanged (the `identity_*` tests, every bundle's `Info.plist` byte for byte), `gg-au-state-tests` passing with the pinned AU faults (carry-over rows 87 and 88 stand; 85, 218 and 232 re-checked, none fixed upstream), clap-validator and pluginval passing; its `cmake/base_sdks.cmake` is unchanged, so the SDKs it pins, `scripts/fetch-sdks.sh` and CI's SDK cache key are too; AUv2's values-only rescan now reports value changes, so local auval goes through the program manager. **The sanitizer jobs' LLVM** to **23.1.4** (`LLVM-23.1.4-Linux-X64.tar.xz`, its SHA-256 checked; `clang-23`, `lib/clang/23`), the version Homebrew's LLVM had for a local `rtsan` that day; RTSan's floor stays upstream LLVM 20. **Inno Setup** to **7.1.1** (`innosetup-7.1.1-x64.exe` from `jrsoftware/issrc`'s `is-7_1_1`, its SHA-256 checked), installed silently on every Windows run and named with `-DGG_ISCC`, so ctest's `installer_windows_inno` and `gg-installer` compile the unchanged script with it; the installer step requires the version the script prints. Not JUCE 9.0.5 (a day old) and no SDK beyond what clap-wrapper's bump brings. **`docs/dependency-watch.md`** is the director's list of pins that trail upstream but stay put for now, each with what would move it and what to check; it isn't carry-over. Part II's §15 (clap-wrapper's pin) and §19 (the Windows installer) are edited. *Decided 2026-11-03 by the director; written by Task 22's implementer.* |
| 91 | The Linux baseline moves to Ubuntu 24.04 | **The director's decision of 2026-11-03** ("can we update the ubuntu image? i think we should update intent 15 to reflect 24.04"), a Part II change made with M2's close (Task 22): **the Linux baseline is Ubuntu 24.04**, and the effect is intended: the Linux products need 24.04's glibc (2.39) and no longer run on 22.04 (glibc 2.35). CI's Linux leg moves from `ubuntu-22.04` to `ubuntu-24.04` and takes gcc-13 from that release's own archive: the `ubuntu-toolchain-r/test` PPA step goes, and `scripts/bootstrap-linux.sh` drops its PPA branch and refuses 22.04 (exit 1, naming the baseline), since its archive has no gcc-13 and a build there would be no baseline's; the fresh-container job runs in `ubuntu:24.04`. The leg's check becomes "Linux products need no libstdc++ or glibc newer than Ubuntu 24.04's": the ceilings are read on the runner, with `strings` on its `libstdc++.so.6` and `libc.so.6`, after both packages are confirmed as Ubuntu's (neither the installed version nor a candidate at or above it from an origin other than ubuntu.com, and an upstream version a noble pocket lists; other PPAs the image carries don't matter), and are not written into the workflow, except that the runner's glibc must be exactly the documented 2.39, so the image drifting from the docs fails; a product needing a `GLIBCXX`, `CXXABI` or (new) `GLIBC` version above them, or exporting a `std::` symbol, fails. With libstdc++ linked statically (kept, §23 #65), glibc is the real floor. GCC 13 stays the compiler, and the `linux-gcc13` preset keeps its name (it names the compiler). The Linux leg's ccache key, which carries the image, misses once; the Linux benchmark baseline's remake (the close's Task 19) uses runs from the 24.04 image. Part II's §15 Linux line and §19's Linux installer line are edited. *Decided 2026-11-03 by the director; written by Task 22's implementer.* |
| 92 | The director's M2 gate verdicts (2026-11-03) | **The M2 listening gate's 40 items** (`docs/listening/m2-gate/README.md`), the director's words: "1) keep, 2) change, 3) yes for hq, but why does 3 tiers have clicks and pops? 4) keep, 5) accept, 6) keep, 7 keep, 8 keep, 9 keep, 10 keep" and "11 no, 12 keep, 13 bounded, 14 fine" and "15 yes, 16 the pad path has a bunch of static in it", "17 accept", and "i don't understand what you're asking in 18; 19 ok, 20 ok, 21 ok, 22 ok23 ok, 24 ok 25 ok, 26 ok" and "all of 40a-h pass" and "32-39 all passed s1-s10" (the host matrix, 2026-11-03), "18 is fine" (in a host) and "16 sounds fine now" (after the re-render), "27 accept, 28 accept, 29 live-input, 30 none, 31 yes" (on 29 he confirmed "Strike live-input"). **1** keep: the pad shares amplitude (D1). **2** change: the director wants a grain stream that continues through the release, built in M3 with the Voice scheduler (carry-over row 404); grains stop spawning at note-off, so Env's release is heard only in the last grains and the reverb's tail. No code changes in M2. **3** yes: HQ earns its weight. Answer to his question: the clicks in the engine pack's `01-three-tiers` were layer B's material, not the tiers (`loop-48k.wav` is 30 ms noise bursts with an instant attack, and 80 ms grains at Spray 100% and +7 replay the onsets: layer B alone scored clickScore 14.2 at Eco, 14.2 at Normal and 13.7 at HQ, layer A alone 2.5, the mix 6.8 to 7.6); `01` is re-rendered with layer B over `sine-330.wav` and scores 2.51 (RMS −22.9, −22.8 and −23.2 dBFS by tier). **4** keep the steal threshold; **5** accept HQ's weak spot (closes row 308); **6** keep the Peak limiter; **7** keep the Spectral kind's width; **8** accept the switch gap; **9** accept the delay's squash; **10** accept Reverb Modulation's bend. **11** none: no preset to cut or redo; **12** keep INIT at Position 0; **13** bounded: Infinite Regress stays bounded; **14** fine: the Mutate ladder's family behavior is accepted, with no filter-type lock. **15** yes: the snapshots look right. **16** yes, on the re-render ("sounds fine now"): the static was `03-pad-path`'s own layer C (white noise at amplitude 0.5, weighted in everywhere but corners A, B and D; clickScore 3.63 as played, 0.00 with layer C off); the pack's `03` is re-rendered with layer C a 550 Hz sine (clickScore 2.02) and the director found it fine. **17** accept: the pad's double-click as two undo entries. **18** no click: the director didn't follow the question as first written, so it was rewritten in plain steps and moved to the host matrix as item 40i; he tried it in a host and said "18 is fine" (row 352 closed). **19 to 25** ok: row 387's strings stand as written (row 387 closed). **26** ok: the gained content's noise floors are accepted (row 394 closed). **27** accept Shard Storm's ticks; **28** accept 17 and 18 as described (row 395 closed). **29** strike the `live-input` tag, keeping every other word; the strike is a code task (Task 17b), and row 396 closes when it lands. **30** none: no rename. **31** yes: the dark browser's list ground looks right (row 397 closed). **32 to 39** pass: all eight cells (Live 12 VST3 and AU, Bitwig CLAP and VST3, both products) passed every step S1 to S10, S5's frozen ring held across save and reopen, S9's editor without attaching (row 232, already closed) and Live AU's S10 (clap-wrapper `4585bc6`) among them; S7's frame-time numbers and S8's thread-pool notes were not given (asked). No carry-over row names these items. **40** pass: 40a to 40h all pass (40i is item 18). **Every item is answered: the gate is complete, 2026-11-03.** S7's frame-time numbers and S8's thread-pool notes were not given (the idle run measures the canvas). The five non-accepts and their homes: 2, row 404 for M3's Voice scheduler (not built now); 3, `01-three-tiers` re-rendered over a sine; 16, `03-pad-path`'s layer C a 550 Hz sine; 18, rewritten as 40i and answered no click; 29, the `live-input` strike, Task 17b (row 396 closes when it lands). Rows closed by the verdicts: 308, 352, 387, 394, 395 and 397. *Decided 2026-11-03 by the director; written by Task 17a's implementer.* |
| 93 | M2's close (sub-project 7): the consistency pass, the burn-down, the build setup, the gate and the release commit | **What the close delivered** (2026-11-02 to 2026-11-03, PR #31; spec `docs/superpowers/specs/2026-11-02-m2-close-design.md`, built). **The consistency pass** (`docs/m2-consistency-review.md`): 8 conflicts decided by the director as #78 to #85, 69 drifts and 2 defects fixed (#89). **The carry-over** burned down from 80 open rows to 15, with rows 403 and 404 added; the 15 are upstream-blocked (85, 87, 88, 218, 233), M3's (184, 404), 1.0.0's and M5's (144, 307), the baselines (305), the watch rows (234, 403), and a record, a widget note and an x86 check (382, 330, 356). **A from-scratch build setup:** the bootstrap scripts (`scripts/bootstrap-macos.sh`, `bootstrap-linux.sh`, `bootstrap-windows.ps1`), a README "Building from source" section and the CI checks that run them in a fresh container. **Diagnostics:** the ring reader's (row 403) and the Rosetta slice's (row 234); neither reproduced, so both stay **watch rows**, not fixes. **The dependency upgrades** (#90, #91) and **the gate** (#92: all 40 items and the eight host cells answered). **The idle figures** (§16, Part II edited, `docs/benchmarks/`): X 19.4% at block 256 against the published 19.5%, the canvas 2.85 ms at 8,192 grains, Space 0.138%, the chain 0.056%, the Spectral stage 0.128% (row 372), HQ 6.87 / 7.42 / 7.88 times Normal (row 307, weights unchanged). **The baselines** remade from three full-length runs at `ca60f54` (`linux-gcc13` and `windows-clangcl`: the EPYC 7763 entries replaced, the 9V74 entries new; `engine-layers-full` settled with no regression on Linux 7763 in two runs). **The US-spelling sweep** (the rename of "programme" and the rest). **Part II text** that earlier tasks added without a row of their own: §18's same-name search step (row 401: a content reference found by its name in the last load folder and under the sample folder), and §14's onset fade, Spectral and `LivePeaks` text. **The release commit:** `GG_VERSION_PRERELEASE` emptied (0.2.0), `docs/releases/v0.2.0.md`, §20's M2 row closed, CLAUDE.md's "Where we are"; the program manager tags `v0.2.0`, a draft that stays unpublished (#62). *Decided 2026-11-03 by the director (the brief's items, #78 to #92) and the M2 close worker.* |
| 94 | M3's program: Voice as a per-layer mode, the LinnStrument and the keyboard, a playable slice first | **The director's decisions of 2026-11-07** (brainstorming; `docs/superpowers/specs/2026-11-07-m3-program-design.md` §2): **(1)** Voice is a scheduler mode per layer beside Cloud (option A of two): a layer's **Mode** is Cloud (today's behavior, the default) or Voice, where every note-on starts a voice with its own stream, pitch, envelopes, expression and a release that keeps spawning (carry-over rows 404 and 184); declined: (B) the instrument always plays voices. **(2)** The players are "a linnstrument or a keyboard": MPE is first-class from the first sub-project, with the sustain pedal and polyphony for pedaled chords (16 voices a layer, stolen with a fade). **(3)** The default expression: pressure to Density and a little Level, timbre (Y, CC 74) to Position, velocity to Level and a little Size ("density shift probably, although you tell me what you think is more musical"; Claude's addition of Level approved). **(4)** Pitch-sync is a per-layer Clock (Free or Pitch) inside Voice mode, the note's frequency the grain rate and Pitch the formant; microtuning is MTS-ESP, then a Scala `.scl`/`.kbm`, then 12-TET; the mod matrix is evaluated per voice on Voice layers ("looks great"). **(5)** A playable slice first (option A of three, "go with recs"): sub-projects 1 Voice slice, 2 Full expression, 3 Pitch-sync, 4 Microtuning, 5 Views and presets, 6 Close; declined: engine first, and three big sub-projects. **(6)** Row 405 is fixed first, its own pull request. **Claude's calls P1 to P8** (the program design's §2a, made overnight at the director's request, "please progress this as much as possible while i'm asleep", and approved by the director on waking): P1 the instrument's INIT stays at Mode Cloud; P2 MPE, MPE Bend Range and Bend Range are instance parameters a preset load keeps, MPE Off by default; P3 sub-project 1 builds the three input paths (CLAP note expressions, MIDI-MPE, `clap_plugin_as_vst3`), the AU's MPE blocked by clap-wrapper (its wish already in §15); P4 a route's Voice-only flag and factory routes 13 to 17, all Voice only; P5 CLAP per-note parameter modulation in sub-project 2; P6 voice handling in-house, so sst-voicemanager's planned §15 row is removed; P7 a per-voice source on a target that isn't per voice reads the newest sounding voice; P8 the pedal and counted note identity reach Cloud layers and the FX too. *Decided 2026-11-07 by the director (1 to 6); P1 to P8 proposed by Claude overnight and approved by the director the same day ("1) approved, 2) approved": the program design and its calls, P1 and P2 by name).* |

**Key sources** (checked 2026-10-26):
- clap-wrapper releases (v0.17.0, 2026-10-20): https://github.com/free-audio/clap-wrapper/releases
- CLAP SDK tags (1.2.10): https://github.com/free-audio/clap/tags
- JUCE 9 announcement (no native CLAP yet): https://forum.juce.com/t/juce-9-is-available-now/69175
- JUCE license (AGPLv3 / commercial): https://raw.githubusercontent.com/juce-framework/JUCE/master/LICENSE.md
- VST3 SDK license (MIT): https://raw.githubusercontent.com/steinbergmedia/vst3sdk/master/LICENSE.txt
- Mutable Instruments eurorack source, including Beads (MIT): https://github.com/pichenettes/eurorack
- Csound partikkel: https://csound.com/docs/manual/partikkel.html
- Visage (GPU UI, CLAP example): https://github.com/VitalAudio/visage
- Granulator III: https://roberthenke.com/technology/granulator3.html
- EmissionControl2: https://github.com/EmissionControl2/EmissionControl2
- dm-TimeWarp: https://github.com/davemollen/dm-TimeWarp
- SignPath open-source terms: https://signpath.org/terms
- shortcircuit-xt (reference template for the CLAP-first layout, CI and installers; GitHub,
  surge-synthesizer organization; local clone `~/src/shortcircuit-xt`):
  https://github.com/surge-synthesizer/shortcircuit-xt
- sst-cmake (the Inno Setup template the Windows installer is adapted from, MIT):
  https://github.com/surge-synthesizer/sst-cmake
- surge (reference template; GitHub, surge-synthesizer organization; local clone `~/src/surge`):
  https://github.com/surge-synthesizer/surge
- OB-Xf (reference template; GitHub, surge-synthesizer organization; local clone `~/src/OB-Xf`):
  https://github.com/surge-synthesizer/OB-Xf
