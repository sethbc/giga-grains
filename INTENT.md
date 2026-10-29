# Giga Grains — Intent

> The open, CLAP-native granular instrument and effect, at giga scale.

| | |
|---|---|
| **Status** | Intent v1 · 2026-10-26 · M0 (Foundations) done 2026-10-26; M1 (Cloud core) closed 2026-10-29 (§23 #60); the installers and release pipeline built 2026-10-29 (`docs/superpowers/specs/2026-10-29-release-pipeline-design.md`, §23 #61; the runbook is `docs/releasing.md`); next: M2 (tagged 0.2.0, an unpublished draft); nothing is public and there are no betas before M5 (§23 #62); v1.0 follows M5 (§20) |
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
    session and managed with splices like Morphagene. Competitors' live buffers are seconds long.
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
  The four kinds are Sample, Live, Oscillator and Resample (§4).
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
  - Defaults to 4,096 grains (`EngineConfig::grainCapacity`; both plugins use the default). A
    larger pool, up to 16,384, arrives with M2's budget manager.
  - Per-layer and per-voice quotas.
  - Dense index arrays with **stable** compaction that preserves spawn order, so per-sample
    summation order (and therefore output) is independent of host block size.
- **Kernels.** "Grain-major" SIMD kernels, vectorized over time within each grain. Templated on
  channel count × interpolation tier × source kind.
  - Tiers: **Eco** = linear, **Normal** = 4-point Hermite (the default), **HQ** = 12- to 16-tap
    windowed sinc.
  - The tier is latched per grain, at its onset. Until M2 only the engine and `gg-render` reach
    Eco; both plugins stay Normal (§23 #43).
  - Run counts are computed per block, so there are no per-sample branches.
- **Grain buses / layer FX.** Partikkel-style channel masks route grains to a few buses per
  layer, each with its own FX. True per-grain filters come later, behind a budget weight.
- **Diffuser.** One per layer, on its output: a short allpass chain that blurs the grains into
  each other, part of the granular sound rather than a room. It is a per-layer unit, so it sits
  in §17's per-layer "Space" panel (since M1 sub-project 4; §23 #50).
- **Mix and global FX.** Four layers feed a mix with XY morph, then delay, reverb and a limiter.
  - The resample tap adds one block of latency.
  - It also has a DC blocker and a soft limiter.

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
optional scale or grid quantize. This is how "shaped randomness" (§8.1) is implemented.

**Two-rate modulation.**
- Continuous controls (layer filters, mix) update on the 32-sample control grid (Cloud core
  rules, below). Mix, Level, Diffusion, Reverb Amount, Dry to Reverb and Feedback are also
  smoothed per sample.
- Grain parameters are **snapshotted when each grain starts**, so each grain gets its own value.

**Macros** (M1 sub-project 5; §23 #58). The eight macros are relative: each is a modulation
source that offsets a few targets from the base values the preset sets, along a fixed factory
recipe per product (in code; user assignments arrive with M2's mod matrix). Each is a CLAP
parameter, 0–1, saved like any other.
- **The effective value** of every parameter is `clamp(base + mod + Σ curves)`, in one clamp,
  never two. Each curve is the target's offset in its own CLAP value domain (octaves for Size and
  Density, semitones for Pitch, dB for Level, the 0–1 domain for the rest), from the macro's own
  effective value `v = clamp(base + mod)`; a macro's own offset is 0. With `b = 2v − 1`,
  `b⁺ = max(b, 0)` and `u = v` (`macroOffsets`, in `gg/core/ParamMap.h`):

  | Macro | Polarity | Offsets |
  |---|---|---|
  | 1 Size | bipolar | Size `+3b`; Density `−1.5b` |
  | 2 Density | bipolar | Density `+4b`; Distribution `+0.25·b⁺` |
  | 3 Pitch | bipolar | Pitch `+24b` |
  | 4 Spray | bipolar | Spray `+0.5b`; Stereo `+0.3·b⁺` |
  | 5 Texture | unipolar | Texture `+min(u / 0.75, 1)`; Diffusion `+smoothstep(0.75, 1, u)`, with `smoothstep(e0, e1, x) = s²(3 − 2s)`, `s = clamp((x − e0)/(e1 − e0), 0, 1)` |
  | 6 Freeze | unipolar, stepped | Freeze `+u` (effective Freeze is on above 0.5) |
  | 7 Feedback (FX) | unipolar | Feedback `+0.9u²` |
  | 7 Stereo (instrument) | bipolar | Stereo `+0.5b` |
  | 8 Mix (FX) | bipolar | Mix `+0.5b` |
  | 8 Level (instrument) | bipolar | Level `+12b` dB |

  A target's offsets add in macro order. Every depth is the spec's starting value, kept as
  shipped at listening gate #1 (§23 #60); M2's mod matrix may revisit them.
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
- **The grain-event log** carries each grain's Texture as `"tex"`, after `"dur"` (§19).
- The gain normalization is unchanged: Texture changes only the envelope.

**Feedback** (the FX; M1 sub-project 5; §23 #58): one layer parameter, 0 to `kFeedbackMax` =
0.95, default 0, that regenerates the wet signal into the live ring. Per sample t and channel,
the ring's input becomes

```
ring input[t] = finite(in[t]) + fb(t) · limited[t − D]        (limited = 0 before the first sample)
```

which the ring then stores as it stores any input (non-finite as 0, clamped to ±2^20). `limited`
is the wet signal after Level and the soft limiter, before the dry/wet mix, so `|limited| < 1`;
`fb(t)` is Feedback's effective value, ramped per sample over 10 ms like Mix.
- **D = 256 = M,** the ring's head margin (`kFeedbackDelay`, static-asserted equal to
  `LiveSource::kHeadMargin`). The admission clamp keeps every grain at least M frames behind the
  head, so the `Processor` renders the FX in pieces of at most 256 samples (it, not the engine,
  owns the pieces): each piece's feedback was rendered before the engine writes the piece into
  the ring, so no grain reads a frame whose feedback isn't there yet. The last 256 limited
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
- **The instrument has no Feedback** until M2's resample source gives it a writable buffer: the
  parameter is hidden there and maps to 0, and its renders keep their old piece size.

**Hard engineering rules**

- **64-bit positions everywhere.** Every position, scan pointer and grain phase is 64-bit:
  int64 + fraction (32.32 fixed point) or double, rebased each block. float32 is allowed only for
  offsets within a block. A float32 position breaks 1000× stretch after about half a second of
  buffer.
- **Deterministic CPU budget.** Each grain-sample costs a fixed weight per tier and channel count.
  When the budget runs out, the engine degrades in a fixed order:
  1. lower the interpolation tier for new grains;
  2. drop new grains;
  3. steal existing grains with pre-emptive fades.

  It **never** adapts to measured CPU load.
- **Mipmaps for upward transposition.** Built offline for files. Built incrementally with
  halfband decimators for live, oscillator and resample buffers.
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
  - **The M1 cap is 600 s** of the file's own audio, whatever the host's rate, checked against
    the header before anything is allocated, and against what a FLAC, MP3 or ADPCM decode
    yields, whatever its header says. M2's memory cap replaces it.
  - **Decoding memory follows what the file holds** (ruling R7, amended by §23 #54).
    Uncompressed audio is allocated once, at most at what the file's bytes can hold. A FLAC, MP3
    or ADPCM header is trusted up to 64 bytes of decoded audio per byte of the file: within
    that, the audio decodes straight into one buffer of the header's count and is held once.
    The cost: a header that lies within the ratio can cost up to 64× the file's size before the
    decode proves the file truncated. Beyond the ratio, or with no count, decoding grows as it
    goes, to about twice the audio that decoded. M2's memory cap remains the real bound.
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
- **Memory management** (M2). A process-wide sample pool removes duplicate copies of the same
  file, and each instance gets a memory cap, shown in the UI; until then the 600 s cap above is
  the only bound.
- **Live ring buffer.**
  - Each block writes before it reads.
  - An admission clamp keeps grains from reaching the write head.
  - The head-crossing policy is the admission clamp with the 256-frame head-proximity fade
    (Cloud core rules, below). Any other policy would be a later, explicit parameter.
  - Freeze and overdub use smoothed gains and crossfaded seams.

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
    position with bipolar jitter.
  - **Per-grain equal-power pan:** `pan = Stereo × bipolar(u)`, with channel gains `√(1 − pan)`
    and `√(1 + pan)`. IEEE `sqrt` is correctly rounded, so the gains are deterministic without
    a polynomial sin/cos.
- **Latched gain normalization.** Each grain's gain is `1 / √max(1, Density × Size)`, with
  Density in grains/s and Size in seconds.
  - That is the expected overlap; uncorrelated grains add in power.
  - It is latched at spawn, so a moving Density can't pump the level.
- **The wet chain:** Σ grains → the layer's diffuser → × Level → soft limiter → mix with dry,
  `dry × (1 − m) + wet × m`, plus the reverb's return while it is active (Space, below).
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

  - **The bracket is added only on samples of an active reverb chunk** (the idle rule, below);
    otherwise `out` is computed exactly as before Space. So with every Space parameter at its
    default the output is bit-identical to before, the sign of zero included, and every earlier
    golden's WAV is unchanged. Every sample is finite, and |out| ≤ 2 + |d|.
  - **The reverb hears what the listener hears:** in the FX, the wet signal scaled by Mix plus
    the dry input scaled by the send, so Mix 0 with the send up puts only the input in the room.
    The instrument has no dry signal; its reverb hears the wet signal.
  - **One output channel:** the engine's mono render is diffused as both sides, the reverb hears
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
    mid-sound it clicks or glitches the pitch. Accepted: Size is "set, don't sweep"
    (`docs/m2-carry-over.md`).
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
    behaviour, and it is kept.
- **The live ring.**
  - Its window is N = 16 s of stereo frames, `[R − N, R − 1]` for write head R. It is stored in
    N + maxBlockSize frames, so a block written whole before it renders never overwrites a frame
    still in a sample's window. Output is independent of the host block size and of
    maxBlockSize (ruling M1-o).
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
  - **Freeze is live until toggled** (§23 #40; ruling M1-ac). The ring isn't saved until M2, so
    a fresh ring starts empty. The parameter therefore engages the engine's freeze only once
    armed, and it arms by being seen off after the activation or the last reset:
    - Freeze is seen where it reaches the engine: each call's first sample, each applied grid
      boundary, and a `setBase` of Freeze itself (params.flush or a state load).
    - An activation and a reset disarm it, and the reset itself doesn't arm. So a Freeze that is
      on when the ring starts, restored with a project, kept through a reactivation, or turned on
      after a reset (a VST3 suspend, say) before audio flows, leaves the effect playing live
      grains until it is toggled. One that is off at the next call's first sample arms.
    - Modulation that takes the effective Freeze off and back on counts as a toggle.
    - The parameter and its saved state still read On while it isn't holding.
    - A boundary's events apply as one group, so off and on again at one boundary is no
      transition, and the result doesn't depend on block size.
    - A note-on needs no arming (Notes, below).
- **Notes** (M1 sub-project 3; §23 #48). Both products take notes; velocity does nothing (D6).
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
  - **The instrument is silent until a note** (the note gate): no grain spawns while nothing is
    held. From the boundary where the last note is released no grain spawns, and the sounding
    ones play out their window, a release tail as long as Size.
  - **Notes freeze the FX** (D4): the engine's freeze is on while Freeze is engaged (armed, as
    above) or any key is held. A note-on engages even right after a reset or a reopened
    project. Releasing every note unfreezes the ring unless Freeze is engaged. Notes never
    change Freeze's value, automation or saved state. Position and Spray keep reading seconds
    behind the stopped head.
- **The instrument's Position and Spray.** Position is the fraction of the loaded file, and
  Spray is seconds, bipolar around it, so its spread doesn't change with the file's length: a
  grain reads from `clamp(position × (frames − 1) + spray × bipolar(u) × sourceRate, 0,
  frames − 1)`. The FX keeps seconds behind the head for both (ruling M1-k).

**Extreme stretch.** Grains alone start to comb and sound static at 100× and beyond. Two tools
address this:
- a **Smear** control that couples position jitter, overlap (8 to 32), micro-detune and
  power-based normalization;
- a paulstretch-style **Spectral Smear** source (random-phase STFT on pffft) that grains can
  themselves read from.

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
- Layers are summed in a fixed order.
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

- C++20 with CMake 3.22 or newer (JUCE 9 requires 3.22).
- Ninja on macOS and Linux; Visual Studio 2022 with ClangCL on Windows.
- Universal macOS builds via `CMAKE_OSX_ARCHITECTURES=arm64;x86_64`, minimum macOS 11.0.
- Linux builds against an ubuntu-22.04 baseline so the binaries run on older glibc.

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
  and the dry/wet mix. The plugin and `gg-render` drive the same `Processor`, so the goldens test
  what a DAW plays. Since sub-project 3 it also holds the session state container
  (`gg/core/SessionState.h`, §18) and the per-machine settings' XML
  (`gg/core/MachineSettings.h`). It has no JUCE.
- **The `media/` layer (`gg-media`, since M1 sub-project 3; §23 #48)** is sample-file I/O, never
  on the audio thread: decoding (`gg/media/Decode.h`), rate conversion (`Convert.h`), the
  SHA-256 fingerprint (`Sha256.h`), loading a sample with its manifest entry (`Sample.h`), the
  search for a missing one (`Search.h`) and the per-machine settings file (`SettingsFile.h`).
  The plugin's loader and `gg-render` share it. It links `gg-core` for the manifest entry and
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
  - Pinned at `4e7afc3` on `next` (2026-10-24, fifteen commits after v0.17.0), which carries
    the AUv2 transport fixes (#613, #615) that no release tag has yet; move to a tag once one
    includes them.
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
in the bullets below (sst-waveshapers, sst-voicemanager, libFLAC, signalsmith-stretch, MTS-ESP
and tuning-library) are planned, for M2 and M3; they are pinned when they are added.

| Library | License | Use |
|---|---|---|
| free-audio/clap, clap-helpers, clap-wrapper | MIT | Plugin API and format wrappers |
| VST3 SDK / AudioUnitSDK | MIT / Apache-2.0 | Fetched by clap-wrapper |
| RtAudio 6.0.1 | MIT-style (adds a non-binding request to send changes upstream) | Fetched by clap-wrapper (CPM tag `6.0.1`, so pinned through clap-wrapper's SHA) for a Linux standalone built without the PipeWire and JACK headers, and on Windows for the standalone's ASIO® only (ASIO is a registered trademark of Steinberg Media Technologies GmbH). Its bundled Steinberg ASIO files have no license grant and are never compiled or on an include path: on Windows it is built with no API of its own, plus its ASIO backend compiled against the Steinberg ASIO SDK below (§23 #30, #56) |
| Steinberg ASIO SDK 2.3.4 (bundled in sst-plugininfra, `libs/asiosdk`) | Used under its GPLv3 option: `common/` files are "Steinberg ASIO License or GPLv3" (its `LICENSE.txt`); the `host/` files carry a BSD-3-Clause-style license | Windows standalone only: `common/asio.cpp`, `host/asiodrivers.cpp` and `host/pc/asiolist.cpp`, unmodified, compiled into RtAudio for its ASIO backend (§23 #56). A distributed Windows build carries the SDK's `LICENSE.txt` with the GPLv3 text, the `host/` files' BSD notice and the trademark attribution (`THIRD_PARTY_NOTICES.md`). The ASIO logo is not used; the plain word "ASIO" in clap-wrapper's audio settings (the API's name and its "ASIO Control Panel..." button) is a descriptive reference to the driver type (the director, 2026-10-29) |
| RtMidi 6.0.0 | MIT-style (the same terms) | Fetched by clap-wrapper (CPM tag `6.0.0`) only with `CLAP_WRAPPER_STANDALONE_RTAUDIO`, which we leave off |
| wil (Windows Implementation Libraries) | MIT | Fetched by clap-wrapper (CPM tag `v1.0.240803.1`) for the Windows standalone |
| JUCE (GUI modules only) | AGPLv3 / commercial | Editor, widgets, accessibility |
| sst-basic-blocks | GPL-3.0-or-later | SIMD wrappers, sinc tables, LFOs, envelopes, mod matrix |
| sst-filters, sst-waveshapers, sst-effects | GPL-3.0-or-later | Filters, drive, reverb and delay |
| sst-voicemanager | MIT | MIDI1, MPE and CLAP note-ID voice management |
| sst-cpputils, sst-plugininfra, sst-clap-helpers, sst-jucegui | MIT | Real-time utilities, versioned patch state, JUCE shim, widgets |
| tinyxml (bundled in sst-plugininfra) | zlib | The patch state's XML, private to `gg-core` |
| simde | MIT | SSE intrinsics on ARM/NEON |
| dr_libs (dr_wav / dr_flac / dr_mp3) | Unlicense / MIT-0 | Audio file decoding: `gg-render`'s WAVs, and `gg-media`'s WAV, AIFF, FLAC and MP3 samples |
| libFLAC | BSD-3-Clause | Encoding embedded captures |
| r8brain-free-src 7.6 (tag `7.6`, `cb2abb9`) | MIT | Sample-rate conversion on load, in `gg-media` (`CDSPResampler24`; header-only, with its bundled Ooura FFT), since M1 sub-project 3 |
| utf8proc 2.12.0 (tag `v2.12.0`, `79cdd5a`; Unicode 18) | MIT (its Unicode data: the Unicode data license, permissive) | Unicode normalization in `gg-media`'s search for a missing sample, so a name saved in NFC is found under its NFD spelling and back; one C file, a static library linked privately into `gg-media`, since the M1 burn-down (§23 #55) |
| pffft | BSD-like | FFT: the metrics' spectra and spectrograms since M0 (§23 #21); Spectral Smear's STFT in M2 |
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
- **Written in-house:** the scheduler, grain pool, kernels and buffers. This is the core of the
  project.
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
- **Rendering will be split into tasks** (M2, with the four layers; §20, §22): layer × grain
  chunk, each with a private accumulator, summed in a fixed order. When the host offers CLAP's
  thread pool (Bitwig does), the tasks run in parallel; otherwise the same tasks run in a loop.
  M1 renders its one layer on the audio thread.
- **No plugin-owned real-time threads in v1.** Neither CLAP nor clap-wrapper can join macOS audio
  workgroups yet.

**Host robustness**

- Handle 0- and 1-sample blocks. Never rely on the declared minimum frame count.
- Keep `params.flush` trivial. The AUv2 wrapper renders silence if its lock is contended.
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
  - **Space's cost is published beside X too** (`gg-bench --space`, report-only, with no ratio or
    baseline; §23 #50): the diffuser and the reverb on stereo noise at 48 kHz, per stereo sample.
    Sub-project 4's fix wave measured 28.49 ns, 0.137% of one M5 Max core, at its final code
    (`ac81dfb`; §23 #50 has the details), and sub-project 6's idle run 29.44 ns, 0.141%
    (`docs/benchmarks/2026-10-29-m1-space.json`; §23 #60); X itself, grains only, is unchanged
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
  full-length benchmark on pushes to `main`, on the nightly schedule and on a manual dispatch (a
  pull request runs `--quick`, a smoke check with no verdict; §23 #57), and their figures go in
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
- **SIMD:** 4-wide via sst and simde, behind an internal batch API. Add xsimd or Highway runtime
  dispatch only if profiling demands it.

### 17. UI and interaction

**Perform view (the default)**
- **The canvas.** Waveform lanes, one per layer. Live grains are drawn where the engine is playing
  them:
  - x = position
  - size = grain length
  - brightness = gain
  - offset = pitch or pan
- **Direct manipulation:** drag the scan window, drag the spray edges, drag vertically for pitch
  spread, freeze, drop a file, record.
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

**Basics**
- Resizable and HiDPI-ready.
- Keyboard-navigable.
- Color-blind-safe layer palette.
- Light and dark themes.

**Canvas rendering**
- **Renderer.** A single `GrainCanvas` component behind a renderer interface.
  - v1 uses a CPU additive "splat" renderer drawing into an image. It works on every platform.
  - A GPU backend gets a timeboxed spike in M2. Candidates are Visage (which ships a CLAP example
    but is desktop-only) and native Metal/GL. The CPU renderer held the §16 UI budget in the M0
    host check, so the spike didn't move earlier (§23 #26).
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
for both products, on sst-jucegui's controls and its built-in dark style sheet:
- **The layout:** a header (the product's name, the preset name, "Init" until M2's browser,
  Perform selected, Edit greyed out until M2, and the editor's frame time averaged over 60
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
  top and hard right toward the bottom, since each grain has one pitch until M2's spread. Under
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
- **The instrument substitutes slots 7 and 8:** Macro Stereo and Macro Level, in place of the
  FX's Macro Feedback and Macro Mix (§14, Macros), until M2's resample source gives it Feedback.
- **Three remote-controls pages** (§18): "Perform" (the eight macros) first, then "Cloud" and
  "Space".
- **Audio → UI, as built** (gg-core, JUCE-free, lock-free and allocation-free on the audio
  thread):
  - **The grain queue:** a `GrainVis` per spawned grain (onset, start position in ring time or
    source frames, rate, length, gain, pan), in spawn order, in an SPSC queue of 4096 that drops
    the newest and counts them when the editor falls behind. The canvas keeps up to 8,192
    sounding grains, dropping the oldest.
  - **The snapshot:** a `UiSnapshot` at the end of every `process()` call (the clock, the write
    head, the freeze setting, the active grain count, Position's and Spray's base and effective
    values, and Feedback), through a triple buffer (`LatestValue`), so the editor always takes
    the newest whole one.
  - **The clock:** the sample clock at the end of the last publishing call, an atomic stored with
    release order after its snapshot. The UI extrapolates it by wall time, never backwards within
    an activation and at most 0.25 s past its last change.
  - **The peak pyramid is one level in M1:** `LivePeaks`, max(|L|, |R|) per 64-frame bucket of
    what the ring takes, indexed by ring time (the frames the write head advanced over, so a
    freeze writes none and a grain's start position reads its own frames; ruling PU-T9a). The
    FX writes it on every render piece whether or not the editor is open, so an editor that
    opens late finds the ring's recent audio, never an older pass's (ruling PU-T9b).
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

### 18. State, presets and identities

**Session state** is a versioned, chunked container (since M1 sub-project 3; §23 #48), in
`gg/core/SessionState.h`:
- **The layout,** every integer little-endian: the magic `GGST`, a u32 container version (1),
  then chunks, each a four-character ID, a u64 payload length and the payload.
- `PTCH`: the patch, using sst-plugininfra PatchBase with migrations. Its bytes are the patch
  XML's, unchanged.
- `MEDI`: the media manifest, as XML: a `<media version="1">` root with zero or one `<sample>`
  (M1 loads at most one sample). A sample's attributes:
  - `sha256`, 64 lowercase hex digits, and `size`, in bytes: the file's fingerprint;
  - `path`, absolute, and `relative`, to the sample folder with `/` separators (empty when the
    file is outside it), both UTF-8;
  - `format` (`wav`, `aiff`, `flac` or `mp3`), `channels` (as stored in the file), `rate` (the
    double's bits, as 16 hex digits, so it comes back bit for bit) and `frames`: the file's;
  - `embed`, always 0 until M2.
- `AUDI` (optional): embedded audio as FLAC. Reserved for M2, and not written yet.
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
  - Unknown chunk IDs are skipped, so a newer build's chunks don't break an older one.
  - A state that starts with `<` is a pre-container state, saved by sub-projects 1 and 2: all
    `PTCH`, with no sample.
  - The plugins refuse a state longer than 64 MiB, and read no further (`kMaxStateBytes`).
- **Writing.** Both products write the container, the FX with an empty `MEDI`. `encodeState`
  never writes a `MEDI` entry that wouldn't decode: debug builds assert, and release builds leave
  it out, losing one sample reference rather than the whole session (ruling R9). A newer
  build's `MEDI` that the instrument loaded is written back byte for byte.
- **`MEDI` records the requested sample, not the one playing.** Only a load or a relink replaces
  it (and a restore sets it), so a session saved while its sample is missing, loading or failed
  to load still names it.

**Sample handling**
- **What gets embedded:**
  - User samples are referenced, not embedded.
  - Captures and freezes are embedded up to **32 MB per instance**. Beyond that, they are written
    to a captures folder as FLAC files named by hash, and referenced from the state.
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
- **Finding a missing sample** (D5). Inside `stateLoad`, each candidate must match the
  manifest's size, then its SHA-256; the first that does loads. The candidates, in order:
  1. the absolute path;
  2. the sample folder plus the relative path (a moved drive, or another machine with the same
     library);
  3. a file of the same name: in the saved path's folder, then in the last folder loaded from,
     then anywhere under the sample folder. That scan visits at most 50,000 entries, never
     follows a symlinked folder, and skips a folder it can't list.

  A match away from the saved path updates the manifest and marks the state dirty. With no
  match, the instrument stays silent and the status reads *missing*, or *changed* when the file
  at the saved path has other content. `gg-render` never searches: a render spec names its file.
- **Per-machine settings:** `settings.xml`, in `~/Library/Application Support/Giga Grains/` on
  macOS, `%APPDATA%\Giga Grains\` on Windows, and `$XDG_CONFIG_HOME/giga-grains/` (or
  `~/.config/giga-grains/`) on Linux. It holds the sample folder, which the user sets once, and
  the last folder loaded from, updated on each load. It is read and written on the main thread
  only, through a temporary file unique to each save, renamed over it (ruling R16).
- **Missing media:** shows placeholders, with relink and a "collect and save" command.
  - In M1 the instrument's loader strip shows *missing* or *changed* with Relink. A file whose
    fingerprint matches restores the sample; another loads as a replacement, and the status
    says so. "Collect and save" comes in M2.

**Presets.** There is one preset system: CLAP preset discovery. clap-wrapper bridges it to VST3
program lists and AU factory presets.

**Frozen from the first public release** (0.5.0's first beta, M5's; §23 #61, #62). Changing any
of these breaks users' saved sessions:
- CLAP parameter IDs (new ones are append-only);
- the AUv2 parameter order that follows from them (ascending parameter ID; §23 #60);
- plugin identities and AU codes.

**Frozen from 1.0.0:** factory preset order, which may move through the 0.5.x releases.

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

**Parameter IDs** (M1 sub-project 1; §23 #37):
- **The scheme is `id = (scope << 16) | index`.**
  - Scope 0 is global; scopes 1 to 4 are layers A to D. Only layer A exists in M1, so M2 adds
    layers without renumbering.
  - Index 0 is reserved.
  - Every ID stays below `0xB00000` (ruling T8-d). IDs are append-only: never renumbered, never
    reused.
- **The first ten,** defined in `gg/core/ParamIds.h`, pinned by a test
  (`tests/core/patch_tests.cpp`) and frozen at the first public release:

  | ID | Name | Range | Default |
  |---|---|---|---|
  | `0x000001` | Mix | 0–100% | 50% |
  | `0x000002` | Level | −60 … +12 dB | 0 dB |
  | `0x010001` | Position | FX: 0–16 s behind the write head; instrument: 0–100% of the loaded file | FX: 0.25 s; instrument: 0% |
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
    is `HIDDEN` and has no effect, and its remote-controls slot is empty (ruling M1-w); so are
    Dry to Reverb and Feedback (below).
  - **Three parameters' `ParamMetaData` differ between the products,** never in ID or range:
    Position (since sub-project 3; §23 #48), in its default and display, the fraction of the
    loaded file in the instrument (§14); Macro 7, "Macro Feedback" at 0% in the FX and "Macro
    Stereo" at 50% in the instrument; and Macro 8, "Macro Mix" in the FX and "Macro Level" in
    the instrument, at 50% in both (since sub-project 5; §23 #58). All are CLAP values 0–1
    under one ID, so the state format stays shared, and a state moved between products keeps
    its stored values. The instrument also hides Mix, Dry to Reverb and Feedback, as above.
    Freeze does nothing in the instrument, which has no live ring.
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
  reserved. The slot order is the ten, Space's eight, then these ten in the order below; there
  are 28 (`kParamCount`). The AUv2 order is ascending parameter ID (above; §23 #60).

  | ID | Name (FX / instrument) | Range | Default |
  |---|---|---|---|
  | `0x010009` | Texture | 0–100% (0 is M0's Hann, 100% gated; §14) | 0% |
  | `0x01000A` | Feedback | 0–95% (`kFeedbackMax`; FX only) | 0% |
  | `0x000011` | Macro Size | 0–100%, bipolar | 50% |
  | `0x000012` | Macro Density | 0–100%, bipolar | 50% |
  | `0x000013` | Macro Pitch | 0–100%, bipolar | 50% |
  | `0x000014` | Macro Spray | 0–100%, bipolar | 50% |
  | `0x000015` | Macro Texture | 0–100%, unipolar | 0% |
  | `0x000016` | Macro Freeze | off/on | off |
  | `0x000017` | Macro Feedback / Macro Stereo | 0–100%: FX unipolar, instrument bipolar | FX 0%; instrument 50% |
  | `0x000018` | Macro Mix / Macro Level | 0–100%, bipolar | 50% |

  - Every one is `AUTOMATABLE` and `MODULATABLE`; Macro Freeze is also `STEPPED`.
  - **The macros are global** (scope 0) and relative: each offsets its targets along §14's
    recipes, and at its default it offsets nothing, so with them at rest a preset renders as its
    base values define it.
  - **Feedback is the FX's:** the instrument marks it `HIDDEN`, and it does nothing there until
    M2's resample source.
  - They joined the patch without a version bump, as Space's did: a state saved before them
    loads them at their defaults and renders as before (a test decodes a literal pre-Perform
    state).
- **The patch** (the state's `PTCH` chunk) is `gg-core`'s versioned XML: patch id
  `org.sethbc.giga-grains.patch`, version 1. Each value also carries its float bits, so a saved
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
  them loads with them at rest and renders as before.
- **Remote controls:** three 8-slot pages, each page's `page_id` its index. "Perform" (since
  sub-project 5; §23 #58), first: the eight macros in order, Macro Size to Macro Mix (in the
  instrument, Macro Stereo and Macro Level in slots 7 and 8). "Cloud": Position, Size, Density,
  Pitch, Spray, Stereo, Freeze, Mix. "Space" (since sub-project 4): Diffusion, Reverb Amount,
  Decay, Size, Damping, Pre-delay, Modulation, Dry to Reverb. In the instrument, the Mix and
  Dry to Reverb slots are empty. Inserting "Perform" moved "Cloud" and "Space" from page IDs 0
  and 1 to 1 and 2, before any release, with their contents unchanged.
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
`remote-controls` and `gui`):
- `params` with per-note modulation flags
- `note-ports` (CLAP, MIDI and MIDI-MPE dialects)
- `voice-info`
- `state` and `state-context`
- `preset-load`
- `tail` (a constant per product, above)
- `latency` (0)
- `audio-ports` (with an optional sidechain)
- `render`
- `remote-controls` (three pages, above)
- `param-indication`
- `track-info`
- `gui`
- `thread-pool` (optional)

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

  No golden pins the audio's bits (§23 #52). The bit-exact checks are:
  - every golden's grain-event log (each grain's onset, position, rate, duration, texture, gain
    and pan: the grains only, not Level, Mix, Feedback's regenerated audio or Space after them),
    compared exactly on every platform. Texture joined the line as `"tex"` in sub-project 5
    (§23 #58), which rewrote every `.events.jsonl` once and left every WAV's SHA-256 alone;
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
    must be bit-identical, and so must a repeated render.
  - Grain-event logs are compared across platforms.
- **Fuzz and property tests** (property tests on the state, the settings and the Processor
  since M1; the sweep harness in M2). Parameter sweeps, extreme values and bursts of automation
  must produce no NaNs, crashes, runaway gain, or clicks above a threshold.
- **CPU benchmark gate in CI:** a regression gate on in-process ratios, from M1's baseline. The
  §16 headline is a published measurement, not a CI gate. Through M1 and the 0.x releases the
  gate reports and never fails; it turns blocking at 1.0.0, after M5 (§23 #60, #61), with the
  published X.
- **Real-time safety.**
  - Clang RealtimeSanitizer on upstream LLVM 20 or newer (Apple clang doesn't ship it).
  - ASan and UBSan.
  - Tripwires that fail tests on allocation or locking in the audio path.
- **Format validators in CI:** clap-validator, pluginval, auval, and Steinberg's VST3 validator.
- **Machine-readable artifacts.** Spectrogram images and summary statistics, so Claude can inspect
  renders and not just assert pass/fail.
- **A human listening gate at every milestone.**
- **CI and release pipeline** (`docs/superpowers/specs/2026-10-29-release-pipeline-design.md`;
  §23 #61). One workflow, `ci.yml`: a `v*` tag, the nightly schedule or a dispatch with
  `installers: true` adds a `gg-installer` target per leg, packed from the very products that
  just passed every test and validator, and a `release` job. Adapted from Shortcircuit XT's
  GitHub Actions, sst-plugininfra's macOS installer script and sst-cmake's Inno Setup template.
  - macOS: a PKG with one component per format (CLAP, VST3, AU, the two apps) inside a DMG,
    installed system-wide, signed with the director's Developer ID and notarized, with the
    certificates and an App Store Connect API key held as GitHub Actions secrets and never
    committed.
  - Windows: an Inno Setup installer and a plain zip, **unsigned at 1.0** (as Surge XT,
    Shortcircuit XT and OB-Xf ship); Azure Trusted Signing or SignPath Foundation's open-source
    program is the upgrade path once downloads make SmartScreen's warning matter.
  - Linux: a tar archive, built on Ubuntu 22.04.
  - Every installer carries `THIRD_PARTY_NOTICES.md`, the AGPL and GPLv3 texts and the platform
    SDK licenses, and each release attaches its complete corresponding source (the tagged tree
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
  reports `0.2.0-dev` and never claims a release; a nightly reports `-dev.<yyyymmdd>.<sha7>`.
  The `plan` job fails a tagged run before anything is built or signed if the tag and the CMake
  version disagree or the suffix is `dev`.

### 20. Roadmap

| Milestone | Delivers | Exit criteria |
|---|---|---|
| **M0 Foundations** (done 2026-10-26) | Repo, CMake, CI matrix (mac/win/linux), `LICENSE` (AGPL-3.0); headless engine skeleton; `gg-render`; test, golden-render, RTSan and benchmark harness; both plugin shells in every format plus standalone; validators in CI. **Spikes:** kernel benchmark (sets §16), JUCE 9 vs 8.0.16, two-descriptor CLAP to AUv2/VST3/standalone, Ableton MIDI routing into an effect, CPU canvas frame budget. | One grain stream renders deterministically. The empty plugins load in the first-class hosts and pass every validator. **Met** (§23 #29). |
| **M1 Cloud core** (closed 2026-10-29; `docs/m1-carry-over.md` is its frozen record, whose 13 open rows moved to `docs/m2-carry-over.md`, §23 #63) | Six sub-projects, each spec → plan → build → review (§23 #35): 1 FX vertical slice (`docs/superpowers/specs/2026-10-27-m1-fx-slice-design.md`); 2 Cloud engine; 3 Sources; 4 Space; 5 Perform UI; 6 Close. Together they deliver: grain pool and SIMD renderer (Eco/Normal; Eco is engine-only until M2, §23 #43), the §16 lever list (§23 #39); Cloud scheduler; parameter model; Sample and Live sources with freeze; note-triggered freeze/capture in the FX (§23 #32; built in sub-project 3 as hold and play, a note freezing the ring and repitching the grains, §23 #48); 1 layer; diffusion/reverb (built in sub-project 4 as Space: a per-layer diffuser and a patched Reverb 2 with an infinite Decay, §23 #50); state save/load; single-lane canvas plus 8 macros | Both plugins make textures. **Listening gate #1: passed 2026-10-29** on the Sources, Space and Perform packs (`docs/listening/`; §23 #60). CPU baseline recorded; the CPU gate reports without failing through M1 and the 0.x releases, and turns blocking at 1.0.0, after M5 (§23 #44, #60, #61). The §16 headline measured on the reference core and published: **19.5%** at block 256 (§23 #39, #60). **M1 closed 2026-10-29** by sub-project 6 (§23 #60; the carry-over's 13 open rows name what they wait on). clap-validator runs in full: M0 skipped its `param-conversions` test, which panics on a plugin with no parameters (ruling T8-c, free-audio/clap-validator#40); sub-project 1 removed the exclusion (§23 #35). |
| **M2 Giga** (its carry-over: `docs/m2-carry-over.md`, 13 open rows from M1 at its start) | 4 layers plus morph; budget manager; no CPU regression vs the M1 measured headline (§23 #41); mipmaps and the HQ sinc tier; minutes-long buffers with persistence; Smear and Spectral Smear; resample/feedback source; full mod matrix; Mutate; factory presets and CC0 samples, including MIDI-driven FX presets (§23 #32); a timeboxed GPU-canvas spike (§17, §23 #26). **Tagged 0.2.0**, a draft release kept unpublished: no beta, nothing public before M5 (§23 #62) | Benchmarks: the report-only gate shows no regressed ratio against M1's baselines (§23 #41). **Listening gate #2.** |
| **M3 Playable** | Voice scheduler, polyphony, MPE, note expressions, polyphonic modulation, per-voice envelopes, pitch-sync/formant grains, microtuning. **Tagged 0.3.0**, unpublished | Listening gate. First-class hosts pass. |
| **M4 Rhythmic** | Sync scheduler: grid, swing, probability, patterns, transport-stop/loop policies. **Tagged 0.4.0**, unpublished | Listening gate. Sync tests pass under loops and jumps. |
| **M5 Synthesis and extremes** | Oscillator sources, synthetic grains, per-grain filters. **Ships as 0.5.0: its betas, `0.5.0-beta.N`, are the first public releases,** and identities and parameter IDs freeze at the first (§18); the public nightly starts here (§19, §23 #61, #62) | Listening gate. Budget weights calibrated. |
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
| Memory blow-up: layers × minutes × mipmaps × instances | RAM cap per instance, shown in the UI; shared sample pool; buffer resolution options |
| Large state chunks slow or crash hosts | 32 MB embed cap; a 64 MiB state read cap; captures folder; background decoding for interactive loads (a session restore loads its sample synchronously, D8, §18) |
| Clicks and level pumping at extremes (head crossing, normalizing thousands of grains) | Engine invariants in §14; click detector in the golden tests |
| sst API churn (no releases); clap-wrapper bugs (transport, AU tail) | Pinned SHAs; upstream fixes; host smoke tests |
| JUCE 9 compatibility of the shim | The M0 spike passed on macOS arm64, so the project is on 9.0.4 (§23 #24). CI run 40445561691 passed on Windows, Linux and macOS (§23 #29), and the macOS leg now also runs the mac-universal x86_64 slice's tests under Rosetta (§23 #33); 8.0.16-security is the fallback |
| Crowded market, including free granular built into DAWs (Granulator III, Bitwig Fragments) | Differentiate on scale, the canvas, openness and portability, not on feature-count parity |
| Open-source granular projects tend to stall | Tight v1 scope around the texture anchor; a published roadmap; verification that lets Claude-led work proceed safely |
| Windows builds are unsigned at 1.0, so users see SmartScreen's unknown-publisher warning once (SignPath's free terms may exclude JUCE's dual license) | Decision 3 of §23 #61: ship unsigned, as the surge-synthesizer projects do; move to Azure Trusted Signing (about $10 a month) or SignPath once downloads make the warning matter |

### 22. Open questions

| Question | Resolved in |
|---|---|
| ~~Plugin identities: CLAP IDs (proposed `org.sethbc.giga-grains`, `org.sethbc.giga-grains-fx`), AU manufacturer code (at least one uppercase letter) and subtypes. Frozen at first release.~~ | **Resolved** in M0 (2026-10-26): see §18 and §23 #20 |
| ~~JUCE 9.0.4 vs 8.0.16-security for the GUI shim~~ | **Resolved** in M0 (2026-10-26): JUCE 9.0.4; see §15 and §23 #24 |
| ~~How Ableton Live routes MIDI into VST3/AU effects (sets how prominent MIDI-driven FX features are)~~ | **Resolved** in M0 (2026-10-26): a MIDI track's "MIDI To" reaches Giga Grains FX as VST3 and as AU, and MIDI-driven FX features are first-class; see §23 #25 and `docs/spikes/m0-ableton-midi-fx.md` |
| ~~Whether the CPU canvas holds its frame budget, or a GPU backend is needed~~ | **Resolved** in M0 (2026-10-26): it holds (under 3 ms of UI-thread time at 8,192 grains in both first-class hosts), and the GPU backend spike stays in M2; see §16, §17 and §23 #26 |
| Which hosts besides Bitwig accept CLAP thread-pool requests under load | M2 |
| Whether grain buses with channel masks are enough for v1 sound design, or per-grain filters are needed sooner | M2 |
| Logic Pro's MPE and tail behavior with the wrapped AUv2 | M3 |
| Default behavior of rhythmic streams when the transport stops: keep running or hold | M4 |
| An optional "Classic" Clouds-faithful mode using sst-effects' Nimbus | Post-v1 |
| Moving from C++20 to C++23: worth it for `std::expected` (the media loader's and state decoder's error paths) and smaller conveniences, but gcc-12's library support is partial and Apple's libc++ gates some C++23 library features by macOS version (we target 11.0); no audio or determinism effect | M2, with a toolchain floor raise (gcc-13 or later) |
| Wider x86 SIMD (AVX2, 8-wide) for the grain kernels: today x86 targets `-march=nehalem` (SSE4.2) and the macOS x86_64 slice Apple's default, with 4-wide kernels everywhere (NEON on Apple Silicon gains nothing). It would need two compiled kernel paths chosen at load by CPU features with an SSE fallback, bit-identical output between paths (as the SIMD kernels already are to the scalar reference; no FMA), a ruling that choosing a path by CPU features (with identical output) doesn't breach §14's no-CPU-adaptive-behaviour rule, and rebuilt x86 `gg-bench` baselines. Measure first, as sub-project 2 did its levers | A timeboxed spike for M2's four layers, if the CI floor legs' informative figures (§16) call for it |
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
| 33 | M0 final-review corrections to Part II | The M0 final review's fix wave corrected Part II without changing a decision: §15 and §21 now give CI run 40445561691's JUCE 9 result (every leg green, Windows ClangCL included), and the macOS CI leg now runs the mac-universal x86_64 slice's unit tests and golden checks under Rosetta; #24 says only the SSE4.1 libwebp unit was seen failing; #23, §21 and `docs/spikes/m0-kernel-benchmark.md` scope the small-block cost to the Linux VM and record that full-length CI runs quantify the gap; §20's M1 exit criteria name the §16 headline re-judgement that #23 already decided; #25 keeps only the director's words; #29 says the event logs match exactly per line, not byte for byte; §18 notes clap-wrapper's VST3 preset selector at `0xC00000`; and the preamble above no longer dates every row to the founding brainstorm. Decided 2026-10-26, during M0 (final review); see §15, §18, §20, §21 and #23 to #29 |
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
| 47 | CI CPU baselines are per CPU model, from separate runs | Found while judging #46's levers: on CI, ratios for unchanged code drifted between runs far beyond the in-run IQR, up to ~20% on Windows, where the two runs landed on different CPU models (GitHub's hosted x86 pools mix AMD EPYC 7763, 9V45 and 9V74 and Intel Xeon 8370C: Linux has run on all four, Windows on the three EPYCs), and up to 6% on Linux on the same model. One run's IQR therefore says nothing about the next run. The controller's ruling: **baselines are per CPU model.** A leg's baseline file holds one entry per `machine`; `gg-bench --make-baseline` groups its input runs by `machine`, and each entry's tolerance is `max(0.10, 2 × (max − min) / median)` over separate CI runs. `--baseline` gives a verdict only against the entry for the run's own machine; on a model with no entry it prints `no baseline for <machine>; no verdict` and exits 0. A model not yet seen gets its entry from later runs on it. The first baselines have entries for the mac leg's Apple M1 VM, Windows' EPYC 7763 and 9V74, and Linux's EPYC 7763 and 9V45. This supports §19's report-only gate through M1; sub-project 6 makes the gate blocking with the published X and adds the models the first baselines lack. Recorded 2026-10-27, during M1 sub-project 2; see §16, §19 and `docs/spikes/m1-cpu-levers.md` ("CI variance") |
| 48 | Sub-project 3 (Sources): notes in both products, the instrument's sample, the loader, the chunked state and the search for a missing sample | The director's eight decisions, by forced choice: **D1** loading is silent and notes start the instrument's sound, the Cloud stream reading the sample repitched to the held notes; **D2** a chord is one cloud, each grain picking one held note at random from the counter RNG, neither mono nor M3's polyphony pulled forward; **D3** a minimal loader in today's editor (Load…, a drop target, the file name and status, Relink…, Sample folder…) until sub-project 5's canvas; **D4** in the FX a note holds and plays: note-on freezes the live ring and repitches the grains to the held notes, and releasing every note unfreezes it, with no capture buffer; **D5** a sample missing on reopen is searched for, then shown missing with relink, never embedded; **D6** the details as proposed: note 60 plays the sample at its own pitch, velocity does nothing, the Freeze parameter or any held note freezes the FX, a note-on engages even right after a reset, WAV, AIFF, FLAC and MP3, conversion to the host rate with r8brain, a 10-minute cap, the instrument's Position as 0–100% of the file with Spray in seconds, a new sample swapping in while old grains finish on the old one, and `gg-render`'s instrument mode, notes and file input; **D7** the state becomes §18's chunked container now (`GGST`, with `PTCH` and `MEDI`), so the format changes once before release; **D8** a session restore loads its sample before it returns, so every render after opening includes it, at the cost of a few seconds' longer open; one `process()` call applies the restored values and sample together, except while two sources are still in play (a swap draining), when that call applies the values and the sample follows within Size (§18). **Rulings made during the build:** decoding memory follows the file's content, never its header alone (R7); truncated files are refused (R8), but an uncompressed WAV or AIFF may declare up to 1 KiB of audio beyond its end, 1024 bytes included (R10, R12), an MP3 may fall two MP3 frames short and a FLAC none; `encodeState` never writes a `MEDI` entry that wouldn't decode, leaving it out in release builds and asserting in debug (R9); MIDI CC 123 releases only its own channel, as MIDI 1.0 says, and an all-off on channel −1 releases everything (R14); a CLAP note-off or choke addressed by `note_id` alone (channel and key −1) resolves through a fixed 256-entry ID table on the audio thread, and an unknown ID releases every note (R15); the settings file is read and written on the main thread only, through a temporary file unique to each save (R16); a note whose Off would round to its On's frame is bad input to `gg-render` (R17). **Ruling R18 is Claude's, made during the build; the director confirmed it on 2026-10-28.** It amends the spec's §6.4, which had the loader convert again in the background when the instrument is activated at a new rate: a reactivation at a new sample rate now decodes and converts synchronously in `activate()`, because the background swap made output depend on the worker's speed, against "no behavior that adapts to measured CPU" (CLAUDE.md) and D8's identical exports. If the file has gone or changed, the old source keeps playing at the right pitch and the status reads Missing or Changed. The cost: a reactivation at a new rate with a long sample blocks the main thread for seconds, as a restore does. **Also recorded here:** the 10-minute cap is 600 s of the file's own audio, and a state is read up to 64 MiB (carry-over T7 218); ruling M1-ac's full Freeze arming rule, and that a note-on engages without arming, now stated in §14 and #40 (carry-over M1-ac); §18's Position, now per product; `gg-media`, the new JUCE-free library, in §15; §21's "async decode" becomes background decoding for interactive loads; §20's M1 row notes that note-triggered freeze is built as hold and play. See `docs/superpowers/specs/2026-10-28-m1-sources-design.md` §4 and §6. Decided 2026-10-28 by the director, during M1 sub-project 3; the rulings made during its build, R18 pending the director's confirmation; see §14, §15, §18, §20 and §21 |
| 49 | Sub-project 3's final review: hardening, and ruling R19 | Claude's, in the final whole-branch review of M1 sub-project 3; the fixes are code and tests, and change no golden. **Ruling R19, confirmed by the director with R18 on 2026-10-28:** a session restore whose sample entry has the current entry's fingerprint (SHA-256 and size), while that sample is in memory, keeps it: no search, no decode, no hash, and the current entry stays. Active, only the values go to the audio thread, through D8's hand-off, in an offer that keeps the source (`SourceExchange::offerValues`); a keep offer that replaces an untaken one takes over its source, so a load not yet taken still arrives. Hosts restore state for undo, A/B compare and preset recall, which had re-decoded for seconds and silenced a sample still in memory whose file had gone; the cost is that an instance that kept its audio differs from a fresh open only if the file vanished mid-session. **Hardening:** gg-media opens WAV, AIFF, RF64 and W64 itself and gives dr_wav a seek that refuses any position outside the file, since a wrapped or huge chunk size (an RF64 `ds64` under 24 bytes, a W64 junk chunk of 2^63) had dr_wav seek forward in 2^31-byte steps for about 47 minutes, freezing the loader, `deactivate()` and `gg-render`; r8brain's MaxInLen is `clamp(65536 × from / to, 1, 4096)` per conversion, since a 1 Hz file converted to 192 kHz had peaked at 7 GB, and to 768 kHz overflowed r8brain's int lengths; a converted buffer keeps at least one frame of a file that has any; XML nested more than 64 deep is rejected before tinyxml parses it (§18), since a crafted project overflowed the stack and crashed the host as it opened; `MEDI` rejects a negative frame count; a WAV whose header declares more than it holds is "truncated" before it is "too long", so an unfinalized recording says so. **Also recorded:** a newer `MEDI` version fails the whole state, deliberately, until the format freezes (§18); for up to one block a fourth source can exist (§14). Decided 2026-10-28 by Claude, during M1 sub-project 3 (final review), R19 pending the director's confirmation; see §14 and §18 |
| 50 | Sub-project 4 (Space): a per-layer diffuser and a patched Reverb 2, on the grid with a 32-sample return, an idle rule, an infinite Decay and a constant tail; and the carry-over policy | The director's eight decisions, by forced choice: **S1** Space is both a diffuser that belongs to the grain sound and a separate reverb with its own controls; **S2** the FX's reverb always hears the grains, plus a separate Dry to Reverb send for the input; **S3** the reverb's controls are Amount, Decay, Size, Damping, Pre-delay and Modulation; **S4** a fresh instance is dry (Diffusion, Amount and Dry to Reverb at 0), and presets choose their own space; **S5** Decay's top is infinite, the input still added; **S6** the reverb is Surge's Reverb 2 from sst-effects, with an in-house diffuser; **S7** at ∞ the LF damping is switched off and the highs still fade by Damping, in a small pinned patch; **S8** Pre-delay is smoothed in the same patch, and Size's artefact when it moves is accepted and logged. **The design** (Claude's calls in the approved spec): the diffuser is per layer, so §14's global chain becomes delay · reverb · limiter; Level and the soft limiter stay before the reverb, which hears the wet signal as the listener does (scaled by Mix in the FX) and whose return passes a soft limiter of its own before the sum, so every sample is finite and |out| ≤ 2 + |d|; the return is added only while the reverb is active, so with every Space parameter at its default both products render bit-identically to before, the sign of zero included, and every earlier golden's WAV keeps its SHA-256; Reverb 2 runs on the 32-sample control grid, one chunk per cell, so its return is a constant 32 samples late at any block size and those samples count toward Pre-delay (its real minimum), with no latency reported; the idle rule: a chunk whose first sample has the Amount ramp at exactly 0 doesn't run the reverb, so raising Amount starts from an empty room; eight parameters appended after Freeze at the next free IDs (`0x01000B`, `0x000003`–`0x000009`), Decay's top 2% shown "∞", Dry to Reverb hidden in the instrument, no patch version bump; a second remote-controls page, "Space"; the CLAP `tail` extension; `gg-bench --space`, report-only. **Rulings made during the build:** P1, Pre-delay glides at no more than 0.5 samples per sample (a fixed 0.1 s glide over 500 ms bent the pitch fivefold), so at most an octave down or a fifth up; P2, at ∞ with Damping up the HF one-pole attenuates the mids on every pass too, so only the lows hold (the listening check judges it); R-T2a, a pre-delay set while nothing has run since a clear applies at once; R-T2b, a clear zeroes only what a later read can reach (about 0.5 MB at 48 kHz, not 14.5 MB), with `clearStep` to spread it; R-T5a, idle chunks spread that clear, 16,384 floats (64 KiB) each, and the first active chunk finishes the rest; R-T5b, `reset()` clears all at once, as hosts expect a reset to cost, and a new rate builds a new reverb; R-T5c, Reverb 2's own decay formula is kept, though the measured T60 at Damping 0 is 1.72, 2.89 and 5.09 s for Decay labels 1, 2 and 4 s (a voicing choice, for the listening check); R-T7a, the tail is a constant per product and never announced: the FX's infinite, the instrument's 122.5 s (Size's 2 s + Pre-delay's 0.5 s + 2 × Decay's 60 s), because clap-wrapper turns `tail.changed` into VST3's `restartComponent(kLatencyChanged)`, a host deactivate and reactivate that would empty the FX's ring and drop its notes and Freeze at every note, Freeze toggle or Amount change, which supersedes the spec's tail that followed the values (the cost: the FX never sleeps on silence, and an instrument at ∞ is suspended 122.5 s after its last note in hosts that suspend silent plugins); R-T8a, at ∞ a 0.5 Hz DC blocker in the loop replaces the bypassed LF damping, since the lossless loop integrated the grains' DC (Task 8's `inst-infinite` golden saw it grow to −0.02); R-T8b (the fix wave after the final review), the switch is continuous: at ∞ the LF dampers themselves run at the DC blocker's coefficient, keeping their state, and entering or leaving ∞ glides over 30 ms in whole blocks (the decay multiplier linearly toward 1, the LF coefficient geometrically, the HF clamp's floor linearly, `loopClip` while any share of ∞ remains), at once before a reverb's first block so one that starts at ∞ is unchanged; before it, entering swapped the LF dampers for a blocker at rest, stepping the loop's lows (a held −32 dBFS pad: +26 dB above 3 kHz over a render that never switched), and leaving dropped the decay multiplier from 1 within one block (+8 dB), and now both stay at least 3.7 dB below. Reverb Decay's typed text takes a bare number as seconds, "ms" for milliseconds (the final review, 2026-10-28). **∞ is bounded by `loopClip`** (`x` up to |x| = 1, else `sign(x) × (2 − 1/|x|)`, below 2, no libm), applied to each block's loop signal after the dampers. **The patch** (§15): a copy of `Reverb2.h` at `987b39d` with every change marked `// GG PATCH`: ∞ (the decay multiplier exactly 1, the LF dampers at the DC blocker's coefficient, the HF clamp at 0, `loopClip`, the 30 ms glide), the smoothed pre-delay, and `clear`/`clearStep`; unused, it renders bit-identically to upstream; sst-effects `987b39d` and sst-filters `87a171b` are pinned with shortcircuit-xt's pins. **Measured:** Reverb 2 is 14,533,632 bytes per instance; Space costs 28.49 ns per stereo sample (min 28.30, IQR 0.09), 0.137% of one M5 Max core at 48 kHz (`gg-bench --space`, release, timed by the fix wave at `ac81dfb`, its final code; the bench and the timed code are unchanged since; load average about 2.7, ordinary desktop activity; sub-project 6 makes the published idle-machine measurement); an hour of full-scale sine or noise at ∞ peaks at 1.685 and 1.718; the first audible return is about 60 ms after the input at Pre-delay 0 and Size 50%, Reverb 2's own taps; moving Size mid-sound over a sine scores 31 to 228 on the golden click detector (limit 30), so `fx-space-automate` moves Size once mid-sound and once in the tail; a Pre-delay glide leaves a broadband haze near −90 dBFS; clap-wrapper's AUv2 hard-codes `SupportsTail()` false, so AU hosts never see the tail. Reverb 2's damping doesn't scale with the sample rate, Surge's behaviour, kept. **The carry-over policy** (the director, 2026-10-28; CLAUDE.md, "Working style"): fix, don't carry, every minor a reviewer raises unless it is blocked outside our control or genuinely large; a sub-project that edits a file closes every open "when touched" row for it; no net growth of open rows over a sub-project; sub-project 6 burns down every open row, M0's included; no external reports or upstream filings, so §15's planned clap-wrapper contributions become wants that wait for upstream, and the Reverb 2 patch isn't offered upstream. The spec's §6.3, §6.4 and §6.7 are amended for P1, P2, R-T2a, R-T2b, R-T5a, R-T5b, R-T7a, R-T8a, R-T8b, `loopClip` and, in §6.3's fork, the policy. See `docs/superpowers/specs/2026-10-28-m1-space-design.md` §4 and §6. Decided 2026-10-28 by the director (S1–S8 and the policy), during M1 sub-project 4; the rulings are Claude's, made during its build; see §14, §15, §16, §18 and §20 |
| 51 | Two open questions added: C++23, and wider x86 SIMD | Two rows added to §22: moving from C++20 to C++23 (for `std::expected` and smaller conveniences, against gcc-12's partial library support and Apple's libc++ gating some C++23 library features by macOS version), resolved in M2 with a toolchain floor raise; and AVX2 (8-wide) grain kernels on x86, which would need kernel paths chosen at load by CPU features with identical output, a ruling on §14's no-CPU-adaptive-behaviour rule and rebuilt x86 baselines, resolved by a timeboxed spike if sub-project 6's measured x86 figure misses §16's target, or for M2's four layers. Neither changes anything yet. Decided 2026-10-28 by the director, during M1 sub-project 4; see §22 |
| 52 | §19 says what is bit-exact; no golden pins the audio's bits | §19 put "Bit-exact comparison applies on the same platform" under golden renders compared by metrics, where nothing compared bits, so a 5% gain bug passed every golden (review finding B-m7, carry-over rows 68 and 103). The event line has since carried each grain's gain and pan (M1 sub-project 1), which pins the grains' level and placement on every platform, but not the output's: the Processor's Level and Mix ramps, the soft limiter, the diffuser and the reverb come after the grains, outside the log, and only the metrics compare them, within 0.5 dB on the RMS envelope and the peak, so a gain error there under about 6% (B-m7's 5% is 0.42 dB) still passes every golden on every platform. What remained was a choice between a per-platform digest of each golden's output and saying plainly what is bit-exact. **The wording, not the digest:** a digest would need a copy per CI platform (macOS arm64, its x86_64 slice under Rosetta, Windows and Linux), refreshed from CI artifacts after every DSP change, and it would fail when a runner's compiler or C library changed, with no change of ours. What it would add is catching a change in the samples, after the grains or in them, that the metrics' tolerances miss. For a change declared output-neutral, a before-and-after SHA-256 of every golden's WAV on one machine catches that at the moment of change, which is when it matters; Space did it by hand (#50), and `scripts/update-goldens.sh` now prints each case's hash and lists the cases whose WAV changed since its previous run in that build tree (a 5% gain on the two Space cases' output, tried and reverted, passed all 23 golden tests and was listed). A change meant to alter the output still relies on the metrics, the spectrograms and the listening gate. So §19 now lists the bit-exact checks: the event log on every platform, the determinism tests on each (now naming the repeated render they already include), the SIMD kernels against their scalar references, and the WAV check for a change meant to leave the output alone, and says that Level, Mix, the limiter and Space are compared by metrics alone. §10's "bit-exact on the same platform" is unchanged; the determinism tests verify it. Decided 2026-10-28 by Claude, during the M1 carry-over burn-down; accepted by the director, 2026-10-29; see §19 |
| 53 | A newer `MEDI` loads with no sample and saves back unchanged; the XML pre-scan follows tinyxml | **The director's decision, by forced choice, on 2026-10-28** (the carry-over row that #49 left open, "A newer `MEDI` version fails the whole state"): a state whose `MEDI` version is newer than the build's loads its patch, keeps the unknown manifest's bytes verbatim so a re-save writes them back unchanged, and reports the status *sample from a newer version*, with no sample loaded. It replaces #49's rejection of the whole state, which kept an older build from dropping a sample reference it couldn't read by refusing the session outright, values and all. Keeping the bytes handles the save-over risk instead: saving the session doesn't lose the reference, and only loading or relinking a sample replaces it, the user's own choice. The manifest must still parse (well-formed, its `<media>` root alone, a whole decimal `version`), so a corrupt one still fails the state; nothing inside a newer one is read. The FX, which has no sample, still writes an empty `MEDI`. **Also recorded (Claude's, in the same burn-down task):** the 64-deep XML pre-scan (§18, #49) could be bypassed, so a crafted project still overflowed the stack. Four document shapes hid nesting from it: an unknown node (`<1 x="<a>…">`), a declaration's quoted values, a numeric character reference (which tinyxml ends at the first `;` and reads back to its `x`), and a UTF-8 lead byte that swallows the next bytes once a declaration or a byte-order mark turns on tinyxml's UTF-8 reading. The scan now follows tinyxml's `Identify`, counts a self-closed element as a level, and refuses what it can't read as tinyxml does: `<?` after the first node, a declaration that doesn't end at its first `>` under every case folding, an unquoted attribute value, a malformed `&#` reference and, in UTF-8 reading, a lead byte without its continuation bytes. A property test checks the scan against tinyxml on random documents. Decided 2026-10-28 by the director (the `MEDI` behaviour) and Claude (the scan), during the M1 carry-over burn-down; see §18 |
| 54 | Ruling R7 amended: a compressed file's header is trusted up to 64× its size | Claude's, in the M1 carry-over burn-down (Task 7, carry-over row 187), **confirmed by the director, 2026-10-29**; controller ruling BD-T7a. R7 said decoding memory follows what the file holds, never only what its header claims, so FLAC, MP3 and ADPCM grew as they decoded and peaked at about twice the audio. Now a header whose frame count is at most 64 bytes of decoded audio per byte of the file (ordinary audio decodes to 2 to 22) is trusted: the audio decodes straight into one buffer of that count, which the `SampleBuffer` adopts, and peaks at about 1.01× the audio. The cost: a header that lies within the ratio costs up to 64× the file's size, allocated and zeroed, before the decode proves the file truncated; before, a liar cost about twice what had decoded. The absolute worst case for a file of a given size is unchanged, since an honest file of near-silence already decodes to far more. Beyond the ratio, or without a count, decoding grows in blocks as before, to about twice the audio that decoded. M2's per-instance memory cap remains the real bound on decoding. Decided 2026-10-29 by Claude, during the M1 carry-over burn-down; see §14 |
| 55 | utf8proc normalizes the names the search for a missing sample compares | Carry-over [190], in the M1 burn-down (Task 8). The search matched file names byte for byte, so a name saved in NFC wasn't found under its NFD spelling (macOS's HFS+ stores names decomposed; ext4 and NTFS keep whichever they're given) or the other way round. Comparing them needs Unicode's normalization tables. macOS has `CFStringNormalize` and Windows `NormalizeString`, but Linux has no system API (ICU isn't a given), and per-platform answers would follow each OS's Unicode version. So **utf8proc** (MIT; its Unicode data under the Unicode data license, permissive and AGPL-compatible), pinned at tag `v2.12.0` (`79cdd5a`, Unicode 18), gives every platform the same answer: one C file, built as a static library without our warning flags and linked privately into `gg-media`. The search tries the saved spelling, then its NFC and NFD spellings, at each step that opens a path, and the scan compares each listed name with the saved one in NFC: canonical equivalence only, with no case folding and no compatibility forms. A name that isn't valid UTF-8 matches only itself. Decided 2026-10-28 by Claude, during the M1 burn-down; see §15 |
| 56 | Windows standalone ASIO, from the ASIO SDK 2.3.4 under GPLv3 | The director decided to enable ASIO in the Windows standalone under the Steinberg ASIO SDK's GPLv3 option (carry-over row 133; the §22 question). RtAudio's own bundled ASIO files still have no license grant, so #30's rule stands: they are never compiled, and RtAudio's `include/` is never on a path. On Windows `libs/CMakeLists.txt` creates RtAudio itself, before clap-wrapper's standalone asks for it, with no API of its own (ASIO, WASAPI and DirectSound off), then adds RtAudio's ASIO backend built against sst-plugininfra's `libs/asiosdk` (ASIO SDK 2.3.4): exactly `common/asio.cpp` ("Steinberg ASIO License or GPLv3", used under GPLv3), `host/asiodrivers.cpp` and `host/pc/asiolist.cpp` (BSD-3-Clause-style), unmodified. Our own stand-in for RtAudio's `iasiothiscallresolver.h` (`libs/thirdparty-impl/rtaudio-asio/`) does two jobs: x64 ClangCL needs no thiscall resolver, so it has none; and it gives the SDK's `ASIOExit()` a non-null `asioDrivers` that never loads a driver, since RtAudio keeps its own and its copy of `asio.cpp` adds a null check the SDK's lacks. RtAudio's other changes to its copies aren't needed: their ANSI registry calls matter only under `UNICODE`, which RtAudio isn't compiled with (the SDK's `asiolist.cpp` doesn't compile with it), and the SDK's `delete` of a one-element array of a trivially destructible struct (theirs is `delete[]`) is a formal mismatch, harmless under the MSVC ABI. The configure fails if any RtAudio source naming ASIO isn't one of those three, if RtAudio's `include/` reaches its include path, or if the SDK's `LICENSE.txt` stops offering GPLv3. The standalone's WASAPI audio and WinMM MIDI stay clap-wrapper's own; ASIO is offered beside them, with the driver's control panel. macOS and Linux are unchanged. The GPLv3-only SDK files join a binary that is already AGPLv3 and GPLv3 (§12). The configure also pins the SHA-256 (line endings normalized) of the SDK's `LICENSE.txt` and of every SDK file compiled or included, so a pin bump that changes one fails until its license is reviewed again; refuses `CLAP_WRAPPER_STANDALONE_RTAUDIO` on Windows (it would drop the native WASAPI); forces RtAudio's JACK and PulseAudio off there; and fails unless both standalones' clap-wrapper libraries define `CLAP_WRAPPER_STANDALONE_ASIO=1` and link RtAudio, so a lost handoff can't build silently without ASIO. **Distribution:** a distributed Windows build carries the SDK's `LICENSE.txt` and the GPLv3 text, the `host/` files' BSD notice, and the line "ASIO is a registered trademark of Steinberg Media Technologies GmbH." in its credits and notices; `THIRD_PARTY_NOTICES.md` holds them. **Trademark** (the director, 2026-10-29: attribution, no logo): Steinberg's ASIO Usage Guidelines in the SDK ask for the ASIO-compatible logo in a configuring dialog where ASIO is enabled manually, and for ® on first use with that attribution line in credits and documentation. We give the attribution (® on first use in our documents, the line in `THIRD_PARTY_NOTICES.md`) and don't use the logo: the plain word "ASIO" that clap-wrapper's settings dialog shows (the API's name and an "ASIO Control Panel..." button) is treated as a descriptive reference to the driver type, and the dialog is clap-wrapper's. Decided 2026-10-28 by the director, during the M1 carry-over burn-down (the trademark ruling 2026-10-29); see §15 and §22 |
| 57 | CI speed-ups, a cache for the SDKs, and the click score's accepted blind spot | **The director's decisions of 2026-10-29,** made during the M1 carry-over burn-down's CI rounds, plus the review's amendments. **CI:** (1) a pull request runs `gg-bench --quick` as a smoke check (no baseline, no verdict, no artifact); the full-length run (§16, §19) happens on pushes to `main`, the nightly schedule and `workflow_dispatch`, and the blocking gate that sub-project 6 adds must choose its pull-request run length, since a quick run's ratios are too noisy for a baseline; (2) ccache on the macOS and Linux legs and the sanitizer job (Windows' Visual Studio generator has no compiler launcher), CI only, so no local preset changes; shallow submodules; the pinned LLVM archive cached by version and SHA-256 (the SHA-256 is checked on a hit too); (3) all caches are restored by every run and saved only on `main` (a push, the nightly, a dispatch), also after a failed build, because GitHub scopes a cache to the ref that saved it and a pull request's is never read; (4) concurrency by pull-request number, or by run id for every other run, so a newer push cancels a pull request's run and nothing else is cancelled; (5) the SDKs that clap-wrapper downloads (VST3 SDK, AudioUnitSDK, RtAudio, RtMidi, wil) are fetched by `scripts/fetch-sdks.sh` at the tags clap-wrapper pins, cached, and passed to Configure as `-DVST3_SDK_ROOT` and its siblings, because CPM's own cache is skipped when a package has a `SOURCE_DIR` (carry-over row 96). None of this changes what is built. **The click score (carry-over row 58):** `metricsVersion` 2 normalizes the click score by the level within ±5 ms and lowers the limit from 30 to 15. It catches 220 of 304 injected -20 dBFS steps; the two dense-noise cases catch none, and none of the same positions' 0 dBFS spikes (scores 9.3 to 14.6 against 15), because their own noise hides them. The director chose to **accept that blind spot** (option A) rather than lower the limit to 10 and exempt `fx-space-automate`'s Size click (11.9) or move that automation. §19's "click detection" says so; `tests/metrics/click_injection_tests.cpp` pins each case's count so a change that moves one is a decision. Decided 2026-10-29 by the director; see §16, §19 |
| 58 | Sub-project 5 (Perform UI): eight relative macros, Texture, Feedback into the live ring, the Perform view on sst-jucegui and the audio→UI path | The director's six decisions (2026-10-27, in brainstorming): **(1)** macros are relative, each a modulation source offsetting its targets from the preset's base values, one writer per parameter; **(2)** Texture sweeps the grain envelope from smooth to gated over the lower three quarters of its macro's travel and adds Space's diffusion in the top quarter; **(3)** Feedback regenerates the wet output into the FX's live ring, and the instrument, with no writable buffer until M2's resample source, has Stereo in its seventh macro slot (the spec's "eighth" corrected, below); **(4)** the eight recipes, every depth tuned by ear at the listening check; **(5)** the canvas: the FX lane's write head fixed at the right edge with history scrolling left, the canvas on top and one macro row below in remote-control order, and a canvas drag editing base values, with ghost windows where modulation moves the grains; **(6)** pan on the vertical axis, sst-jucegui's controls on the existing JUCE 9 shim, §17's audio→UI path, a fresh seed per instance (#59), and Edit, Mod, Mutate, the preset browser and lanes B–D left in M2. **The design** (the spec and the plan, as built): ten parameters appended after Dry to Reverb at the IDs the spec reserved, Texture `0x010009`, Feedback `0x01000A` (0–0.95, hidden in the instrument) and Macros 1–8 `0x000011`–`0x000018` (Macro Freeze stepped; Macros 7 and 8 named per product, Macro 7 resting at 0 in the FX and 0.5 in the instrument), 28 in all, with no patch version bump (§18, `6af2583`); a "Perform" remote-controls page of the macros first, "Cloud" and "Space" after it, `page_id` = position; the effective value `clamp(base + mod + Σ curves)` in one clamp, each macro's own value `clamp(base + mod)`, the fixed factory recipes of §14 in each target's CLAP value domain, plain float with no libm, every offset exactly +0.0f at rest (bipolar 0.5, unipolar 0) so a resting preset is bit-identical, on the 32-sample grid (`962e153`); Texture's fixed family (`aMin = min(0.5, max(0.05, 16/d))`; τ ≤ 0.5: `a = 0.5 + 0.4τ`, `r = 0.5 − 0.4τ`; τ > 0.5: the lerp from (0.7, 0.3) to (aMin, aMin), sustain between), the window `hann(warp(φ))` with one `warpPhase` for the scalar references and the SIMD interior, the identity bit for bit at Texture 0, four doubles latched per grain, and `"tex"` in the grain-event line, which rewrote every `.events.jsonl` once and moved no WAV (`f54cb85`); Feedback's ring input `finite(in[t]) + fb(t)·limited[t − 256]`, `limited` after Level and the soft limiter, before the mix, `fb` a 10 ms ramp like Mix's, `fb ≤ 0.95`, D = 256 = M (the ring's head margin, static-asserted), the FX rendered in pieces of at most 256 samples so the output is bit-identical at any block size and maxBlockSize, a per-sample guard that writes the input alone wherever `fb` is 0, Freeze winning because the frozen ring's write gain is 0 (`711f5fe`); the publish structures in gg-core, `SpscQueue<T, N>` (the grain queue of 4096, dropping the newest and counting them), `UiSnapshot` in a triple buffer and `LivePeaks` (`4061e72`), filled by the `Processor` from its own grain sink, with the atomic clock stored after each snapshot (`d406a91`); the UI→host path, a 1,024-entry `SpscQueue<UiParamChange>` drained by `process()` and `params.flush` before the host's events, every change sent on as CLAP gesture and value events by `try_push` after `request_flush` (`b1cbe6e`); `LaneModel` and `SplatRenderer::addCapsule` (`796f29e`), the lane clock and live-grain list (`f822e30`); the Perform view (§17): header, `GrainCanvas`, the macro row on sst-jucegui with its dark style sheet, 900 × 500 down to 640 × 360, and the instrument's loader strip with carry-over rows 198 and 199 closed (`d179a98`, `fe4de01`); three goldens, `fx-macros`, `fx-feedback` and `inst-texture` (§19), and the cross-feature block-size-invariance and allocation tests (`bac40b4`, `7ab5880`, `3d614bd`). With Texture 0, Feedback 0 and every macro at rest, every earlier golden's WAV kept its SHA-256 through every task. **Rulings made during the build:** PU-T8a, the FX's scan window is one-sided, `[Position, Position + Spray]` behind the head, as the engine draws a live grain (`position + spray × u`), its newer edge dragging as the body; the instrument's stays `Position ± Spray` (`b18b185`); PU-T3a, Texture's τ > 0.5 branch is the lerp `(1 − s)·0.7 + s·aMin`, the plan's curve, which lands exactly on its anchors where the plan's `0.7 + (aMin − 0.7)s` gives 0.050000000000000044 for 0.05; PU-T2a, a `setBase` of a macro jumps the ramps of every target it moves (Mix, Level, Diffusion, Feedback) and Macro Freeze is seen as Freeze for arming, so a loaded state never ramps in from stale values; PU-T4a, the freeze test checks the frozen ring bit for bit under Feedback 0.9 and the output's level with Feedback raised at the freeze, not the plan's 0.5 dB comparison, since regeneration before the freeze legitimately sits in the frozen buffer; PU-T7a, sst-jucegui's JUCE requirements are re-pointed at the headers-only `gg-juce-headers` plus `$<LINK_ONLY:clap_juce_shim>`, so JUCE compiles once, in the shim, and the configure fails if the wiring it replaces changes (`038b53a`, `6acc5fa`, `01f979a`); PU-T9a, the live peaks are indexed by ring time, the frames the head advanced over, not the sample clock, which a freeze makes diverge; PU-T9b, the FX writes the peak strip on every render piece, publishing or not, so an editor opening late never shows an older pass's peaks, while the grain queue and the snapshot stay gated on the editor (`c193784`); PU-T10a, the plugin owns one editor context for its life and updates its publish pointers in place on activate and deactivate, null while inactive, since the `Processor` they point into dies with deactivate; PU-T12a, `fx-macros` runs over a 330 Hz sine, not noise, which hid all sixteen injected clicks (a blind spot the director hadn't accepted), and `fx-feedback`'s input lasts 4 s, so Feedback 0.9 at 3 s has material (`3d614bd`); PU-T5a, the instrument's seed (in the loaded-state snapshot) and values (with its sample) may reach the audio thread in different blocks, accepted since a live load's timing is inexact anyway and the saved state is consistent (#59). PU-T13a (sub-project 5's documentation review), the 16-sample floor binds only Texture's gated anchor, `aMin = min(0.5, max(0.05, 16/d))`: for τ ≤ 0.5 the shape is Hann-like and its attack and release scale with the grain, as the default Hann always has (an 8-sample attack on a 16-sample grain at Texture 0 is M0's behaviour, kept bit for bit), which amends the spec's §4 "never shorter than the 16-sample minimum grain"; PU-T11a, sst-clap-helpers' shim keeps the editor of a GUI a host destroys without ever showing it (no `set_parent`), and the next `guiCreate` trips its debug-only `jassert(!editor)`; publishing is correct either way (`EditorPlugin::guiDestroy` turns it off), and the fault waits on upstream in the carry-over. **The spec's corrections:** sst-jucegui is MIT (its `LICENSE.md`; §15 already said so), not GPL-3.0-or-later; the Feedback chunking is the `Processor`'s, not the engine's, since the `Processor` owns the render pieces; decision 3's Stereo replaces the instrument's seventh macro slot (Macro 7), not its eighth, which is Level. **Also recorded:** the peak pyramid is one 64-frame level in M1 (§17); a −0 base plus a −0 modulation now gives +0 once the resting offset is added (host-only input; no golden moves); the editor's 1,024-entry queue drops a change made while it is full, which takes a host that ignores `request_flush` (and doesn't process) for more than 1,024 changes, and a dropped gesture begin or end leaves the host an unbalanced gesture: an as-built exception to §15's lossless rule, accepted in Task 10's review and confirmed by the director 2026-10-29 (D1, below); in the 60 s Feedback 0.95 property test the wet RMS reads −0.18 dBFS over both seconds 16–26 and 50–60: it settles into self-oscillation just under the limiter, with no runaway (`711f5fe`; a listening-check item). **The director's decisions after the final review** (2026-10-29, by forced choice): **D1** the UI→audio parameter queue is accepted as built (1,024 entries, dropping a change only when a host ignores `request_flush` while more than that are queued), so §15's as-built exception stands, confirmed by the director 2026-10-29; **D2** auval lists the AU parameters in ascending CLAP-ID order, while §18 says the slot order is the AUv2 order: decided by the director 2026-10-29 to amend §18 to ID order in sub-project 6, by a row here, once clap-wrapper's AU `ParameterList` order is verified, with no code change (a carry-over row until then); **D3** the canvas frame budget is judged by the densest setting a host can reach staying at or under 3 ms in Live and Bitwig, plus an idle `gg-bench --canvas` run at 8,192 grains at or under 3 ms, taken with sub-project 6's X measurement (a run with nothing building, load average about 4, measured 2.80 ms, M0's 2.77 ms; the idle run is sub-project 6's), decided by the director 2026-10-29 (§16; the spec's §9); **D4** `fx-feedback`'s click-injection entry, 7 of the 16 steps caught, is accepted by the director 2026-10-29 (§19). The spec is amended in place for decision 3's slot (§1), PU-T2a (§3.1), the family, PU-T3a and PU-T13a (§4), the `Processor`'s pieces, the per-sample guard and PU-T4a (§5), PU-T8a (§6.2), PU-T9a, PU-T9b and PU-T10a (§6.4), MIT (§6.5), PU-T5a (§7), and PU-T12a and D3 (§9). See `docs/superpowers/specs/2026-10-27-m1-perform-ui-design.md` §1 and `docs/superpowers/plans/2026-10-29-m1-perform-ui.md`. Decided 2026-10-27 by the director (decisions, in brainstorming), 2026-10-29 by Claude (rulings) and 2026-10-29 by the director (D1–D4, after the final review), during M1 sub-project 5; see §14, §15, §16, §17, §18, §19 |
| 59 | A fresh seed per instance, and the seed hand-off | Sub-project 5's spec §7 (the director's decision 6, #58) and carry-over rows 115 and 123, closed by `5ac8a86`. **The fresh seed:** a new instance draws its engine seed on the main thread at construction from the OS's entropy source, two 32-bit draws from `std::random_device`, falling back to a splitmix64 of the steady clock if it throws, so two instances on two tracks make different clouds; the seed is saved in the state as before, so a reloaded session renders identically, and the audio thread's randomness still comes only from `gg::rng`, keyed by it. `gg-render`, the tests and the goldens keep explicit seeds; the plugin tests that compare instances load a state with seed 0 first. It replaces "new instances start at seed 0" (§18). **The hand-off (row 115):** a state loaded while active used to need the host to honour `request_restart` for its seed to take effect, and otherwise the engine kept the old seed while `stateSave` wrote the new one, so a reload sounded different from what was heard, latent while every instance used seed 0. Now the plugin's loaded-state snapshot carries the seed in both products, the audio thread calls `Processor::setSeed` (`Engine::setSeed`), which keys every grain scheduled from that `process()` call on (sounding grains keep their draws, and the grain index keeps counting), and `stateLoad` asks for no restart. In the instrument the values travel with the sample, so the seed and the values may land in different blocks (ruling PU-T5a). **The value mirror (row 123, ruling M1-x):** once the audio thread has applied a loaded state's values, it sets a flag and requests a callback, and `on_main_thread` asks the host to rescan the values (`CLAP_PARAM_RESCAN_VALUES`), since the first rescan, at the load, may have read a value a block in flight then overwrote. Decided 2026-10-27 by the director (the fresh seed, in the spec's brainstorming) and 2026-10-29 by Claude (the hand-off), during M1 sub-project 5; see §14 and §18 |
| 60 | M1 closed: X published as measured, the CPU gate blocking at 1.0, listening gate #1 passed, the AUv2 order is ID order | **The director's decisions of 2026-10-29,** made in sub-project 6's brainstorming, and the sub-project's measurements. **(1) The CPU gate stays report-only through M1 and turns blocking at 1.0,** the first public release, not in sub-project 6 as §19, §20 and #44 said; those sentences are amended, and carry-over rows 144 and 148 move to "At 1.0". **(2) Listening gate #1 is passed:** the director listened to the Sources, Space and Perform packs (`docs/listening/m1-sources`, `m1-space`, `m1-perform`) and accepted them; the macro depths stay at the spec's starting values (the Perform pack's tuning step closes as accepted as shipped); no further pack. **(3) The AUv2 parameter order is ascending parameter ID** (sub-project 5's decision D2, #58, carry-over row 233): verified in clap-wrapper `v0.17.0`'s `WrapAsAUV2::GetParameterList` (`libs/clap/clap-wrapper/src/wrapasauv2.cpp`), which, when a plugin provides no `CLAP_PLUGIN_AUV2_PARAM_ORDERING` extension (these plugins don't), falls back to `AUBase::GetParameterList`, and that lists the AudioUnitSDK's `flat_map` of parameters, a vector sorted by parameter ID (`AUScopeElement.h`); auval's listing agrees. §18's "slot order, and so the AUv2 order" is amended to say so, in both places and in the frozen list; the slot order itself is unchanged, and no code changes. **(4) X, measured and published** (§16): one idle session on the director's M5 Max, 2026-10-29, the `release` preset, at `be2291e` (main's engine at M1's close), the full default `gg-bench` run: `engine-sample` 2.44 / 2.21 / 2.03 ns per stereo grain-sample at blocks 32 / 64 / 256, **X = 19.5%** of one core at block 256 (23.4% / 21.2% at 32 / 64); the live figure 31.9% / 25.8% / 21.0%; Space 29.44 ns per stereo sample, 0.141%; the canvas 2.87 ms per frame at 8,192 grains (p95 2.92), inside sub-project 5's 3 ms budget (D3). The one-minute load average was 2.58 before the run, above the design's bar of 1.5, with nothing building, testing or benchmarking and only the desktop open; every in-run IQR under 3% of its median. The JSON is in `docs/benchmarks/` with a README. The ratios against the frozen `reference` kernel, 1.154 / 1.072 / 1.024, sit within 3% of sub-project 2's (carry-over row 185 closes). **(5) The carry-over's end state:** 15 open rows before sub-project 6, 13 after (185 and 233 closed); the 13 that stay open wait on upstream bumps (85, 87, 88, 218, 231), on M2/M3 features (51, 118, 119, 184, 217), on the installers (221) or on 1.0 (144, 148), each with its reason in the row. **Non-goals held:** no DSP, engine or UI change (the goldens' 22 WAV hashes unchanged, trivially); no installers or release pipeline (their own spec, next); no new CI baselines. *Decided 2026-10-29 by the director, during M1 sub-project 6; written by the Claude session that closed M1.* |
| 61 | Installers and the release pipeline decided; v1.0 moves to after M5; milestone numbering; the freeze splits | **The director's decisions of 2026-10-29,** made in the release-pipeline brainstorming (`docs/superpowers/specs/2026-10-29-release-pipeline-design.md`, approved the same day), and the docs consistency pass that recorded them. **(1) The pipeline's first job is the public releases,** not nightlies; nightlies come only because the same job on the existing schedule costs nothing. **(2) macOS builds are signed with the director's Developer ID and notarized** from GitHub Actions secrets (the two certificates as a `.p12`, an App Store Connect API key); nothing private is committed, and a pull request never sees a secret. **(3) Windows ships unsigned at 1.0** (route 1, as Surge XT, Shortcircuit XT and OB-Xf do); Azure Trusted Signing or SignPath Foundation's open-source program is the upgrade path; §21 and §22 amended. **(4) Public releases precede 1.0.0, numbered by milestone:** M2 ships as 0.2.0, M3 as 0.3.0, M4 as 0.4.0, M5 as 0.5.0, betas as `0.x.0-beta.N`, then 1.0.0; `main` carries `0.<next>.0-dev` ("that seems cleaner" than a year of `1.0.0-beta.N`). **(5) The freeze splits:** plugin identities and AU codes and the CLAP parameter IDs (with the AUv2 order that follows from them) freeze at the first public release, 0.2.0's first beta; factory preset order and the blocking CPU gate wait for 1.0.0. §18's "frozen at first release", §16, §19, §20 and carry-over rows 144 and 148 are amended; #60's "1.0, the first public release" is superseded in its second half. **(6) The pipeline extends `ci.yml`** (a `gg-installer` CMake target per leg, a `release` job), not a second workflow and not CPack. **(7) The shape:** a PKG with one component per format inside a DMG, installed system-wide; an Inno Setup installer plus a zip; a tar archive; every installer carries the notices and licenses (carry-over row 221's installer half). **(8) v1.0 moves in the roadmap to after M5 and before M6:** M2 to M5 ship as 0.x releases first, 1.0.0 is M0 to M5 polished against §10, M6 Reach follows; §20 amended, Part I untouched. A consequence the director accepted: the blocking gate and the baselines for unseen CPU models (rows 144, 148) wait until after M5. **(9) The secrets are set during the pipeline work,** so a CI dry run is part of it, and the builder may sign locally with the keychain's identities through the environment. **(10) The public nightly starts with M5;** until then the nightly keeps its signed installers as run artifacts behind a committed switch. **(11) Each release ships its complete corresponding source** as a tarball of the tagged tree with every submodule at its pin, with the SDK tags named beside the binaries. **The docs pass** (same day, `docs-polish`): INTENT's header, §8 and §12 (Part I, with the director's sign-off: overload order; the Unicode data license and the ASIO SDK's GPLv3 option in the compatible list), §14, §15, §16, §17, §18, §19, §20, §21 and §22 brought into line with the as-built record and these decisions; CLAUDE.md and README likewise. *Decided 2026-10-29 by the director; written by the Claude session that drafted the release-pipeline spec.* |
| 62 | Nothing is public, and there are no betas, before M5 | **The director's correction of 2026-10-29,** given while the release-pipeline plan's first task was running: "we should not be doing betas or considering release public until M5". So #61's "M2 ships as 0.2.0, with betas" becomes: M2, M3 and M4 are **tagged** 0.2.0, 0.3.0 and 0.4.0 and each produces a draft GitHub Release that stays unpublished (only collaborators see a draft), the director's own installers for host testing and the pipeline's rehearsals; **no betas before M5**; M5's `0.5.0-beta.N` are the first public releases, then 0.5.0 and 1.0.0. **The freeze point follows:** plugin identities, AU codes and CLAP parameter IDs (with the AUv2 order) freeze at `v0.5.0-beta.1`, not 0.2.0's; preset order and the blocking CPU gate stay at 1.0.0; the public nightly still starts with M5 (#61 decision 10). §18, §19 and §20 amended; the release-pipeline spec and plan likewise. *Decided 2026-10-29 by the director; written by the Claude session running the plan.* |
| 63 | The carry-over splits for M2 | **The director's request of 2026-10-29,** made while the release-pipeline plan ran. `docs/m1-carry-over.md` becomes M1's frozen record: a paragraph under its title says so, and nothing else in it changes after row 221's narrowing, made just before the split. Its 13 open rows (51, 85, 87, 88, 118, 119, 144, 148, 184, 217, 218, 221 and 232; row 221 narrowed to the About box, its installer half closed by the release pipeline's commits) move to `docs/m2-carry-over.md` with their numbers; that file repeats the policy's five rules unchanged and is the carry-over from now on, its new rows numbered from 300. The no-net-growth count is that file's open rows, 13 at the split. §20's M1 and M2 rows, the pointers in §14 and §15 and CLAUDE.md point at the M2 file. (A row's number is the line it stood on before the freeze; in the M1 file each now stands 5 lines lower. #60's list names the shim row 231; it stood on line 232.) *Decided 2026-10-29 by the director; written by the Claude session that wrote the release procedure.* |

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
