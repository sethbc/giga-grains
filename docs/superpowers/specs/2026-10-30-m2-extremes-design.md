# M2 sub-project 4: Extremes and the FX chain — design

| | |
|---|---|
| **Status** | **Approved by the programme manager 2026-10-30** against the programme design and INTENT, with three notes applied in place (§4, §6.3, §13): the named edits inside sub-projects 3's and 2's files are accepted as described, resolved at this sub-project's rebase with the `Field` numbers assigned then; the `isValidTarget` layer rule is sub-project 3's, found done after the rebase and only tested here; the §23 row takes the next free number at merge. **The director's decisions D1–D8, 2026-10-30,** relayed by the programme manager, are recorded in §8 and applied in §6: every recommended option (D1–D8 option 1). Written by the sub-project's worker session from the programme design's §6.4 and §7, which it realizes and does not reopen. **Built 2026-10-30** (Tasks 1 to 16 on `m2-extremes`; INTENT §23 #NN records it). Sections changed by the build are marked **Amended**; the director's later decision D7a (2026-10-30) is in §8 and §6.4 |
| **Milestone** | M2 Giga (INTENT §20, §23 #64), sub-project 4 of 7; wave 2, with sub-projects 3 and 2, **merged third** |
| **Spec authority** | `INTENT.md`, then `docs/superpowers/specs/2026-10-29-m2-programme-design.md` (its §6.4, §7 and §12 are this sub-project's charter). This document records how this sub-project implements them |
| **Builds on** | M2 sub-project 1 as merged (`ab9f4e2`, PR #15; INTENT §23 #65): the four layers, the per-layer chain of §7.2, the Source-kind parameter with its enumeration (Spectral = 3 exists and falls back), the mod matrix, the reserved index block `0x..60`–`0x..7F`, the render tasks; M1's Space (`core/src/space/`, the diffuser, the soft limiter, the 32-sample grid) |
| **Feeds** | Sub-project 5 (presets voice the filters, the delay, the limiter and Smear; the Spectral kind); sub-project 6 (the Edit view's Filter panel and the FX view draw these parameters); sub-project 7 (the pack, the chain's cost beside X, INTENT §22's buses row) |
| **Starts from** | `main` at `ab9f4e2` (sub-project 1 merged), and `docs/m2-carry-over.md` with **11 open rows** |
| **Rebases on** | sub-project 3 (merged first: the pool, the HQ tier, the budget), then sub-project 2 (merged second: the ring length, the resample tap, `MEDI` v2), before its final verification (programme design §6, "Wave rules") |

## 1. Why this sub-project

INTENT §5 promises 100× to 1000× stretch and §3 names extreme sound design as a focus; §14's
"Extreme stretch" says grains alone comb and go static there and names two tools, Smear and a
paulstretch-style Spectral Smear source. §14's Units draw grain buses with per-bus FX and a filter
per layer, and its Mix and global FX draw a delay and a limiter after the layers; §17 gives them an
Edit panel and an FX view; §22 asks whether buses with channel masks are enough for v1 or per-grain
filters are needed sooner. The programme design (decision 3, "everything is M2"; §6.4; §7.7) puts
all of it here, in wave 2, beside the engine's scale (3) and the sources' persistence (2).

It delivers the programme design's inventory items I20 to I23:

- **I20 Smear:** one layer parameter that couples position jitter, overlap (8 to 32), micro-detune
  and power normalization, so a cloud at extreme stretch stops combing (§6.1);
- **I21 Spectral Smear:** a source kind the grains read, a random-phase STFT on pffft whose phases
  come from the counter RNG (§6.2);
- **I22 buses and filters:** two grain buses per layer with a channel mask per grain, each bus
  with its own filter on sst-filters, on the 32-sample grid with per-sample smoothing; and §22's
  buses question answered with a measurement (§6.3, §6.10);
- **I23 the global chain completed:** a delay and a real limiter beside the reverb, both resting
  as the identity so M1's output is unchanged (§6.4, §6.5).

**Headless, like wave 1.** Every new parameter is reachable through the host's generic editor,
automation, the mod matrix, new remote-controls pages and `gg-render`; the Edit and FX views that
draw them are sub-project 6's (programme design §6.6).

## 2. Goals

1. **Smear** as one parameter per layer, 0 at rest: at 0 every grain field is M1's bit for bit;
   at 1 a 1000× stretch shows no comb where Smear 0 does (the exit criterion, measured by a
   pinned test, §10).
2. **The Spectral Smear source** as the fourth source kind in both products, deterministic across
   platforms by its grain-event log and within the metric tolerances by its audio, read by the
   same kernels as the live ring at every tier, HQ included once sub-project 3 lands.
3. **Two buses per layer, a filter each,** on the 32-sample grid with per-sample coefficient
   smoothing, declicked on every switch; with the split at 0 and the filters off, the layer's
   grain sum is M1's bit for bit.
4. **A global delay and a global limiter** after the layers' sum, before the mix, each the exact
   identity at its default: with every new parameter at rest both products render M1's bytes,
   and every one of the 22 M1 goldens, and sub-project 1's 14, keeps its WAV SHA-256 through
   every task.
5. **§22's buses question answered** with a measured cost of a per-grain filter against the
   bus filters (§6.10), recorded in INTENT §22 and §23.
6. **No CPU regression on X:** `engine-sample` shows no `regressed` ratio; the new cases and the
   chain's cost are published beside it, report-only.
7. **Deterministic and real-time safe** throughout: bit-identical at every block size and with
   any thread pool, no allocation on the audio thread, RTSan-clean.

## 3. Non-goals and anti-goals

**Non-goals (other sub-projects, or later):**
- The budget manager, the pool's ceiling, the HQ tier and the mipmaps (3); the ring's length,
  the memory cap, the resample tap and kind, `AUDI` and `MEDI` v2 (2). This sub-project adds no
  chunk and bumps no version (programme design §7.5).
- The Edit view's Filter panel, the FX view, the layer strip (6); presets that use any of this (5).
- Per-grain filters (INTENT §14: "True per-grain filters come later, behind a budget weight", M5).
  This sub-project measures their cost (§6.10); it does not ship them.
- Tempo-synced delay times (M4's sync scheduler owns tempo); a second delay line or a multitap.
- A global DC blocker (§14 lists one with the soft limiter; nothing today needs it, and the reverb's
  ∞ already has its own). A new row in §22 notes it.
- More than two buses, or per-bus pan, level or sends: two buses with a filter each are what §7.7
  asked for, and the measurement decides what follows.
- Spectral parameters beyond what the layer already has: the STFT's size, hop and window are fixed
  (§6.2); Position, Spray and Freeze give the Spectral kind its controls.
- Smoothing Reverb Size (carry-over row 217): sub-project 6 decides it in the FX view (programme
  design §8). This sub-project adds files beside `Reverb2Patched.h` and doesn't edit it.

**Anti-goals (a failure even if everything works):**
- One bit of difference in any M1 or wave-1 golden with Smear 0, the buses unsplit, the filters
  off, the delay at Amount 0 and the limiter Off.
- A click or a zipper when a filter's type, cutoff or resonance moves, when Bus Split moves, when
  Delay Time moves, when the limiter's mode changes, or when a layer's kind switches to or from
  Spectral.
- Output that depends on the block size, the thread pool, the host's rate conversion of nothing, or
  measured CPU; a `std::` distribution or a clock-seeded RNG anywhere, pffft's phases included.
- An allocation, lock or syscall on the audio thread, the STFT included: every buffer and pffft
  setup is made at construction.
- A parameter outside `0x..60`–`0x..7F`, a new scope, a patch version bump, or an edit inside
  another sub-project's files beyond an addition.
- A limiter that adds latency, or a delay whose tail grows without bound or goes non-finite.

## 4. Constraints

- CLAUDE.md's non-negotiables: the audio-thread rules, RTSan-clean, 0- and 1-sample blocks,
  `params.flush` trivial; randomness only from the counter RNG keyed by (seed, layer, stream,
  field, grain index); no `std::` distributions; no CPU-adaptive behaviour; layers summed in a
  fixed order; block-size invariance; 64-bit positions; the overload ladder's order; licensing
  (pffft is BSD-like and already built, INTENT §15; sst-filters is GPL-3.0-or-later and pinned).
- INTENT §8's principles break ties: §8.1 (Smear must sound good across its whole range), §8.2
  and §8.3 (the Spectral kind is a configuration of the one engine and a source like any other),
  §8.4, §8.6 and §8.9.
- The programme design's §7: my parameters in the `0x..60`–`0x..7F` blocks of scope 0 and of each
  layer scope, absent → default with no bump (§7.1, §7.5); the chain of §7.2 with the delay and
  limiter where it draws them and both resting as the identity (§7.7); the limiter's default
  equal to M1's soft limiter so no golden moves; every M1 hash kept and X unchanged in definition
  (§7.6); routes reach the new parameters through the matrix's existing target model (§7.3).
- **Ownership** (§6.4 "Owns"): the `Processor`'s wet chain after the grain sum (buses, filter,
  diffuser, delay, limiter, mix), `core/src/space/` and the new FX units, the Spectral Smear
  source as a new source kind. Shared files by addition only: `Processor.h`/`.cpp`,
  `ParamIds.h`/`.cpp`, `ParamMap.h`/`.cpp` (`builtKind`, which sub-project 1 named for 2 and 4 to
  edit), `ModMatrix.h` (the new globals' target rule), `Engine.h`/`.cpp` (the spectral slot and a
  second output pair), `tests/CMakeLists.txt`, the plugin's remote pages, INTENT.
  **Edits at named places inside files other wave-2 sub-projects own or touch,** each a few
  lines, resolved by this sub-project at its rebase (it merges last) and named in its SPEC READY
  report so the programme manager can object (the builder check found that pure additions won't
  do):
  - `engine/src/Engine.cpp` and `Engine.h`: a third source slot (`kSpectralSlot`), which means a
    branch in the spawn path, in `admit` (`lastEnd` is a sample slot's alone) and in `renderPool`
    (the live kernel over the spectral ring), a ring argument on `liveGrain`, a second output pair
    on `process`. Sub-project 3's ladder rewrites `Engine.cpp`'s admission (row 51), so the rebase
    onto 3 conflicts there by design;
  - `engine/include/gg/grain/CloudScheduler.h` and `CloudScheduler.cpp` (sub-project 3's): two
    `CloudParams` fields and the Smear and bus draws inside `schedule()`'s body, which is in the
    header, beside the spreads' draws; a Spectral case in `setSourceMode`'s per-mode sanitizing;
  - `engine/include/gg/grain/GrainPool.h` (3's): a `bus` bit in the grain row;
  - `engine/include/gg/core/Rng.h`: four appended `Field` values, whose numbers are assigned at the
    rebase in merge order, so this sub-project's goldens are re-rendered then;
  - `core/include/gg/core/ModMatrix.h`: `isValidTarget`'s layer rule, which today admits a layer
    parameter only below `LFO 1 Rate` (so that no modulator's control is a target) and must
    instead refuse the modulators' controls by name, or every wave-2 layer parameter is silently
    dropped as a route. **Assigned to sub-project 3 by the programme manager (2026-10-30),** which
    merges first: this sub-project finds it done after its rebase and only tests that its eight
    layer parameters are admitted (§10); until the rebase its own branch carries the same
    one-line change so its tests and goldens run, and drops it at the rebase. The new globals
    need no change;
  - `engine/include/gg/core/MathDet.h` and `core/include/gg/core/ModSources.h`: `sineDet` moves
    into the engine, bit for bit (the Spectral source's phases need it; `ModSources` keeps using
    it from there).
  The Spectral source itself lives in a directory of its own, `engine/include/gg/spectral/` and
  `engine/src/`, not in sub-project 2's `gg/source/`. If any of these turns out to need more than
  the edit named, the worker stops at the boundary and says so (the wave's rule).
- The process: spec → plan → subagent-driven build → fresh review → fix wave; `scripts/check.sh`
  over `dev,asan,rtsan`; CI green through a draft PR; `scripts/update-goldens.sh` before and after
  each DSP change with the hash list in the commit; the carry-over counted at start and end.

## 5. Approaches

**Where the Spectral Smear source's output lives.** Three were considered.

1. **The source renders into a ring of the live ring's type, which the grains read as they read
   the live ring (chosen).** The STFT engine writes one sample per output sample into a
   `LiveSource` of its own (5 s), and a Spectral grain is a ring grain with its own slot
   (`kSpectralSlot`), so `renderLiveSimd<Reader>` and its scalar reference serve it at every tier
   with no new kernel: sub-project 3's HQ reader reads it the day it lands, and the kernel tests
   that pin SIMD to reference over a ring cover it. *Cost:* about 1.9 MB more per layer at 48 kHz,
   allocated whatever the kind (a kind switch may not allocate), and the source's output is a
   constant one STFT window late, which a stationary texture never shows.
2. *A frequency-domain source the kernels read directly* (grains index STFT frames and bins).
   Rejected: a new kernel family per tier, nothing of M1's read contract reused, and §8.3's uniform
   interface broken.
3. *Render the smear offline into a sample slot* (a stretched copy of the sample, made on the main
   thread). Rejected: it can't smear the live ring, it allocates minutes of audio per layer, and a
   Position move would mean a new render.

**The buses.** Two were considered.

1. **The engine renders each grain into the bus its row names; the layer's task filters each bus
   and sums them (chosen).** The pool row gains a bus bit latched at spawn, and `Engine::process`
   takes a second output pair. *Cost:* a second accumulator per layer, and the sum of two buses
   re-associates the float sum whenever bus 2 holds a grain, which is why bus 2 must be provably
   empty at rest (§6.3).
2. *Filter per grain in the kernel.* Rejected for v1 by INTENT §14 itself (behind a budget weight,
   M5); measured here as §22 asks (§6.10).

**The filter.** sst-filters' `CytomicSVF` (chosen, pending D4): per sample, SIMD stereo, modes LP,
HP, BP and Notch among others, with `setCoeffForBlock<32>` and `processBlockStep` doing exactly
"coefficients on the grid, interpolated per sample", and a libm-free coefficient path (`fasttan`
is plain arithmetic). The alternatives are D4's.

**The delay.** In-house (chosen), after reading sst-effects' `Delay.h`: its needs here are a
grid-latched time with a rate-limited glide (Space's Pre-delay rule, P1), a soft-clipped feedback
loop, a one-pole damper, ping-pong and an exact bypass, a few dozen lines of plain arithmetic.
sst-effects' Delay brings tempo-sync plumbing, its own modulation LFO, width and crossfeed logic and
a block-level `lipol` we would have to disable or re-derive the determinism of. The in-house unit
is cross-platform bit-exact; the sst one would not be (its coefficients use libm).

**The limiter.** The per-layer soft limiter is M1's and stays; the global stage is a new unit with
a mode switch whose default is Off, the identity (§6.5). The stronger mode's law is D5's.

**Smear's coupling.** One parameter driving four couplings through fixed curves (chosen, as §14
and I20 say), rather than four parameters: a single knob that always sounds good across its range
is §8.1; the curves are §6.1's and their strength is D8's.

## 6. Design

### 6.1 Smear (I20)

`Smear`, per layer (`0x..60`), 0–100%, default 0. With s its effective value in [0, 1], D the
layer's Density in grains/s, S its Size in seconds, and every draw keyed by (seed, layer, stream,
field, k) as the other per-grain fields are:

- **Overlap.** The target overlap is `T(s) = 8 + 24s` (D8 option 1; 8 + 8s under option 2). The
  effective density the scheduler runs is
  `D_eff = D + s × (min(400, max(D, T(s) / S)) − D)`: continuous at 0 (D_eff = D), at 1 the density
  that gives the target overlap, never below D and never above the plugins' 400 Hz ceiling (so at
  tiny Sizes the floor isn't reached). A change of s or S moves the pending onset by ruling M1-h's
  rescale, with no new draw.
- **Position jitter.** Each grain's position gains `s × J × S × bipolar(u)` seconds, `J` = 0.5
  (option 1) or 0.25 (option 2), `u` keyed with `Field::SmearJitter`: in Sample mode added in
  seconds at the source's rate as Spray's seconds are (`sprayOffsetSec`), in Live and Spectral mode
  added to the seconds behind the head, the admission clamp applying as it does to Spray. It is
  tied to Size, not to Spray, because the comb at extreme stretch comes from overlapping grains
  re-reading the same material at the same offsets, and Spray is usually 0 there on purpose.
- **Micro-detune.** Each grain's pitch gains `s × C × bipolar(u)` semitones, `C` = 0.5 (option 1)
  or 0.2 (option 2), `u` keyed with `Field::SmearDetune`, inside the existing ±48 clamp, after
  Pitch Spread's term.
- **Power normalization.** The latched gain stays `1 / √max(1, D_eff × S)`: it follows the raised
  overlap, so Smear doesn't pump the level, and the decorrelated grains add in power as the law
  assumes (M1's law was written for uncorrelated grains; Smear is what makes them so).
- **At 0 nothing is drawn** and no field changes: `D_eff = D`, the jitter and detune terms are
  skipped (a stateless draw has no side effect), so every M1 and wave-1 grain is bit for bit, and
  the event log is unchanged. At any s > 0 the event line's `pos`, `rate` and `gain` carry it, so
  the cross-platform gate sees Smear.
- **Where it runs.** `toCloudParams` (`ParamMap.cpp`, an addition) computes `D_eff` and the two
  amplitudes into two new `CloudParams` fields (`smearJitterSec`, `smearDetuneSemitones`; an
  addition to sub-project 3's header) and the raised `densityHz` and `gain`; the draws sit beside
  Pitch Spread's and Size Spread's in `CloudScheduler::schedule()`'s body, which is in the header
  (§4). The 400 Hz cap is the Density parameter's ceiling (the engine itself clamps at the sample
  rate), so Smear can't push a layer past what the knob can. The budget manager (3) counts the
  raised density like any other.
- **The comb test** (the exit criterion): two in-process instrument renders over a noise sample
  at a static Position (an infinite stretch), Size 200 ms, Density 20 Hz (overlap 4), Smear 0 and
  Smear 1, 8 s each; over the last 6 s the test takes the power spectrum in 1 s windows, its log,
  and the real cepstrum of that; the comb from grains re-read every 1/D shows as a cepstral peak
  near quefrency 1/D and its multiples. The score is that peak's height over the median cepstrum
  in the 10–500 ms quefrency band. The test requires Smear 0's score above a limit and Smear 1's
  below a lower one, both pinned as exact constants the builder measures (§15), at least 6 dB
  apart, and the same at 1000× (Position moved by 0.002 of the sample over the 8 s through LFO 1).

### 6.2 The Spectral Smear source (I21)

**What it is.** A paulstretch-style source: at every hop it takes one window of the layer's
material around Position, randomizes every bin's phase from the counter RNG, resynthesizes it and
overlap-adds the result into a stream of its own. The stream is a stationary, evolving smear of
the material with no transients and no comb; grains read it as they read the live ring.

- **The material** is D1's. Under option 1: the layer's sample when it holds one (the slot
  current at the hop), otherwise its live ring. Position addresses the material as the layer
  would read it as Sample or Live: a fraction of the sample, or seconds behind the ring's head
  (M1-k's one-sided seconds); Position's text follows the material (ruling FR-6's rule, from
  Source's base value and the loader's state).
- **The STFT.** Size N is the power of two nearest 85 ms at the running rate (4096 at 44.1 and
  48 kHz, 8192 at 88.2 and 96 kHz, 16384 above), hop H = N/4 (a multiple of 32, so every hop
  boundary is a grid boundary). Analysis and synthesis windows are both Hann, whose product at a
  quarter hop sums to the constant 1.5, which the synthesis divides out, so a steady material
  keeps its RMS within 1 dB (a test). **Amended:** the synthesis divides by 0.75
  (`kOverlapGain`), not 1.5. The phases are redrawn every frame, so the overlapping frames add in
  power, not in amplitude, and the 1.5 derivation was wrong; the stated behavior stands (measured
  −0.144 dB). Frame k is computed when the output clock reaches
  absolute sample kH, from the material at the layer's Position in effect at that boundary, and
  its N output samples are accumulated for spectral time [kH, kH + N); the stream's sample at
  output time t is the accumulation at spectral time t − N, so every sample written is complete.
  pffft's real transform (`PFFFT_REAL`, N a multiple of 32, 16-byte aligned buffers) does the
  forward and inverse transforms; its setup and every buffer are allocated at construction, the
  work buffer included (pffft takes it from the stack when none is given).
- **The phases.** Bin b of frame k takes the phase `2π × uniform01(hash(seed, layer, 0,
  Field::SpectralPhase, k × (N/2 + 1) + b))`, DC and Nyquist left real; **Amended:** the stream
  in the key is the channel, not 0, so the channels' phases are independent and the smear fully
  decorrelates them (center content comes out wide); the magnitude is the
  analysis frame's. `sineDet`'s quarter-wave polynomial gives cos and sin, libm-free. So the smear
  of a given seed, layer, Position history and material is a function of the sample clock alone,
  at any block size.
- **The output ring** is a `LiveSource` of `SpectralSource::kRingSeconds` = 5 s (Spray's 2 s,
  Size's 2 s, the 256-frame margins and a block), written one sample per output sample on every piece, whatever
  the layer's kind: silence while the kind isn't Spectral, so a layer that isn't Spectral pays a
  write of zeros and no STFT; the smear while it is. Its head never freezes. A grain of the
  Spectral kind is a ring grain on slot `kSpectralSlot` = 3 (`Engine::kLiveSlot` is 2): the live
  kernels render it over this ring, at the grain's tier, and the grains sounding at a kind switch
  finish on the source they started on, as wave 1's rule says. Switching to Spectral starts the
  STFT at the next hop boundary: the stream fades in over one window (the first frames'
  overlap-add), under the ring's 256-frame margin, so the switch doesn't click; switching away
  stops the STFT and writes silence, and the last Spectral grains read what is there.
  **Amended:** switching away first drains the pending overlap-add (the frames already begun
  finish into the ring), then writes silence. Writing silence at once clicked (a second
  difference 1,000 times a steady signal's), against the §3 anti-goal, which binds.
- **Grains over the smear.** A Spectral grain's start is `Spray × u` seconds behind the spectral
  head, one-sided as a Live grain's (`Field::PositionJitter`, the same draw), under the same
  admission clamp and head-proximity fade; Position never addresses the spectral ring.
  **Amended:** Position on ring material is bounded as on a Live layer, `max(1, liveBound)`
  seconds behind the head (the floor of 1 keeps a sample-material Spectral layer usable when the
  live ring is under 1 s or absent; at the products' 16 s the bound equals the Live case's, and
  a test pins that). The build's first brief had a 5 s bound, which confused the live ring with
  the spectral one. Pitch,
  Size, Texture and the spreads act as on any layer; Smear's jitter adds to the seconds behind
  the head.
- **Freeze** on a Spectral layer (D2). Under option 1: it freezes the layer's live ring exactly as
  on a Live layer (the 20 ms write-gain ramp, the stopped head), so ring material under the
  window holds still and Position scans the frozen ring; sample material is still already. The
  smear itself keeps evolving, frame by frame, with new phases: a spectral freeze. The spectral
  ring is never frozen.
- **Feedback** on a Spectral layer is 0, as on a Sample layer: Feedback writes a Live layer's ring
  (wave 1's rule, kept); a Spectral layer's ring is its material. `toWetParams` keeps the rule.
- **Memory** per layer at 48 kHz: the ring 5 s × 2 channels × (240,000 + maxBlockSize) floats,
  about 1.9 MB, plus the STFT's frame, accumulation and work buffers (under 0.2 MB); about 8.4 MB
  per instance, recorded in INTENT §14 beside wave 1's 45 MB (the cap is sub-project 2's).
- **CPU.** Two real FFTs of N per hop per channel: about 50 Mflop/s per Spectral layer at 48 kHz,
  under 1% of a core, measured by `engine-spectral` (§6.10).
- **The material is decided where a sample is offered.** **Amended:** it is decided where the
  engine *adopts* a sample (`takeOffers` and `setSourceNow`, which call `syncMaterial()`), since
  an offer not yet taken, or a source taken but not adopted, is superseded and must not change
  the units. The `Processor` owns each layer's
  `SourceExchange` and hands its samples to the engine, so it knows whether a layer holds one;
  a Spectral layer's `CloudParams` are computed with the material's units (Position a fraction, or
  seconds behind the head) through a `toCloudParams` overload that takes the material, and are
  re-sent when a sample is offered to or cleared from a Spectral layer, as they are on a kind
  change. The engine's scheduler draws a Spectral grain as a Live grain with Position 0 (Spray
  alone, one-sided); the STFT reads Position from the `CloudParams` in the material's units. The
  text rule (FR-6) and `CLAP_PARAM_RESCAN_TEXT` (§6.8) follow the same flag.
- **`builtKind`** (sub-project 1's one fallback function) returns a built kind for Spectral in
  both products; `SourceMode` gains `Spectral`, and the engine's `setSourceMode` takes it.
  Resample and Oscillator still fall back. Sub-project 3's HQ reader (`LiveSource::readSinc` and
  its widened ring guard) serves the spectral ring unchanged, since it is a `LiveSource`.
- **Determinism across platforms.** pffft is built with NEON on arm64 and SSE on x86, so the
  smear's audio differs in its last bits across platforms, as Reverb 2's `powf` does; the
  goldens' metric tolerances cover it. The grain-event log doesn't see the audio (every field is
  drawn, not measured), so the cross-platform gate stays exact for Spectral cases.

### 6.3 Grain buses and the per-bus filters (I22)

**Two buses per layer.** Each grain carries a bus bit, latched at spawn (D3). Under option 1: the
grain takes bus 2 when `uniform01(hash(seed, layer, stream, Field::Bus, k)) < Bus Split`, the
layer's `Bus Split` (`0x..61`, 0–100%, default 0); at 0 nothing is drawn and every grain is on
bus 1, M1's path. The engine renders every grain into its bus's accumulator in spawn order (a
second output pair on `Engine::process`, null for M1's one-bus call, which `gg-bench`'s
`engine-sample` keeps using), so each bus's sum is spawn-ordered and the pair is a fixed order.

**A filter per bus:** `Bus 1 Filter`, `Bus 1 Cutoff`, `Bus 1 Resonance`, `Bus 2 Filter`,
`Bus 2 Cutoff`, `Bus 2 Resonance` (`0x..62`–`0x..67`). The type is stepped, **Off, Low-pass,
High-pass, Band-pass, Notch**, default Off; Cutoff is 20 Hz–20 kHz, log (log2 Hz), default 1 kHz;
Resonance 0–100%, default 0, mapped onto the filter's damping so that every setting is stable
(the builder pins the map, §15). The model is D4's; under option 1 it is `CytomicSVF`, 12 dB per
octave, SIMD stereo.

- **On the grid, per-sample smoothed:** at every grid boundary each active filter takes its
  effective cutoff and resonance (`setCoeffForBlock<32>`), and `processBlockStep` interpolates the
  coefficients across the cell's 32 samples, so automation and the matrix never zipper. Cutoff's
  Hz comes from its log2 value through `exp2Det`; the filter's own coefficient path is plain
  arithmetic (`fasttan`), so the filtered audio is expected bit-exact across platforms, and the
  metrics compare it in any case.
- **Exact bypass.** A bus whose type is Off is not filtered: its buffer passes bit for bit. Bus 2
  is **provably empty** while no grain has been admitted to it since the last `reset()` or since
  the layer turned on: a sticky flag the engine raises at the first bus-2 admission and the layer
  clears where it clears its diffuser and Feedback history. **Amended, as built:** the engine
  owns the flag, `bus2Used()`, raised at the first admission of a bus-2 grain (either
  `process()` form) and lowered only by `Engine::reset()` and by the enable gate turning off, the
  two places the layer clears its diffuser and Feedback history. While it is down, bus 2's
  filter is still stepped, on silence, so it stays silent and its state is rest when the flag
  rises. While the flag is down the layer's
  sum is bus 1 alone, with no `+ 0`, so a layer with Bus Split 0 and both filters Off is M1's
  grain sum bit for bit, the sign of zero included. Once the flag is up the sum is
  `bus1 + bus2`, each after its filter, for the rest of the layer's run: a live count would drop
  bus 2's filter tail from the sum the moment its last grain ended, a click, while the extra
  association after a bus-2 grain is harmless.
- **Declicked switches.** Each bus holds two filter instances. A type change (Off included) starts
  the incoming type on the spare instance at the boundary, from rest, and crossfades the bus from
  the outgoing instance's output to the incoming's over the 10 ms ramp (the outgoing keeps running
  through the fade, then clears); after the fade the spare is free again. A change inside a fade
  turns the fade from its current value, as the ramps do. A cutoff or resonance move is smoothed by
  the per-sample interpolation alone. Bus Split moves latch per grain, so they can't click.
  **Amended (the final review's fix wave):** the first cell after a clear (a new instance, a
  reset, the layer switched back on) takes its type at once, as the ramps jump there, so a loaded
  or re-enabled filter applies from its first sample rather than fading in from Off.
- **Where in the chain:** before the diffuser, inside the layer's task (`renderLayerPiece`), so the
  diffuser, Level, the soft limiter, Feedback's history and the morph weight see the filtered sum
  exactly where they saw the plain sum. Nothing after the sum changes.
- **Mod targets.** The six filter parameters, Bus Split and Smear are per-layer targets like any
  layer parameter (by layer A's slot, on one layer or every layer); the stepped type is a target as
  Freeze is. `isValidTarget`'s layer rule changes from "below LFO 1 Rate" to "not a modulator's
  control", sub-project 3's change (§4), which this sub-project's routes rely on and test. The new
  globals (§6.4, §6.5) take no layer, as Mix and the reverb's do, and need no change there.

### 6.4 The global delay (I23)

After the layers' weighted sum and before the reverb's return and the mix, as the programme
design's §7.2 draws it: `wet' = delay(wet)`, the reverb hears `wet'`, and the mix takes `wet'`.

Five globals (scope 0, `0x60`–`0x64`): `Delay Amount` (0–100%, default 0: the return's gain),
`Delay Time` (1 ms–2 s, log, default 375 ms), `Delay Feedback` (0–100%, default 40%; the top is
D7's), `Delay Damping` (0–100%, default 30%: a one-pole low-pass in the loop, its cutoff from
20 kHz at 0 down to 500 Hz at 100%, log) and `Delay Ping-pong` (stepped off/on, default off).

- **The law,** per channel c, with `y_c` the line's output read `Time` samples back (linear
  interpolation at the fractional read), `in_c` the layer sum, `f` Feedback and `damp` the
  one-pole: `line_c[t] = in_c[t] + f × softLimit(damp_c(y_src[t]))`, where `y_src` is the same
  channel's output, or the other channel's with ping-pong on; the output is
  `wet'_c = in_c + Amount × y_c`. The loop runs through M1's `softLimit`, so with `f` ≤ 1 it is
  bounded below 1 and, at `f` = 1 and Damping 0, holds a repeat's level like Decay ∞ (D7
  option 1), but not its color. **Amended (D7a, the director, 2026-10-30, option 2):** the hold
  slowly darkens bright material. Damping 0 maps to a 20 kHz one-pole (coefficient 0.927), not
  to no damper, so at Feedback 100% a noise burst loses about 2 dB in the 2–4 kHz band over 20 s,
  and a sine burst holds within 0.1 dB. The alternative, Damping 0 bypassing the damper for an
  exact hold, was declined.
- **Exact identity at rest:** the line is written every sample (cheap, and raising Amount reveals
  what was already playing, as the diffuser does), and the return is added only on samples where
  Amount's per-sample ramp isn't exactly 0: no `+ 0`, so `wet' == wet` bit for bit, the sign of
  zero included, with every delay parameter at its default or Amount at 0.
- **Smoothing.** Amount, Feedback and Damping ramp over 10 ms like Mix. **Amended (the final
  review's fix wave):** so does Ping-pong, as a weight p that moves the damper's input from the
  channel's own read to the other's, `y_c + p × (y_other − y_c)`, exactly `y_c` at p = 0 and the
  other read at p = 1, so a toggle never clicks (a hard switch stepped the echoes); like the other
  ramps it jumps where they jump (a loaded state, a macro or route, a clear). **Time** is D6's: under
  option 1 the read position glides toward its target at no more than 0.5 samples per sample
  (Space's Pre-delay rule, P1), so a move bends the pitch like tape and never clicks; while nothing
  has run since a clear the new value applies at once (R-T2a). The ramps and the glide are
  functions of the absolute sample, so the output is block-size invariant.
- **Memory:** 2 s of stereo at the running rate plus a block, about 0.8 MB at 48 kHz, allocated
  with the `Processor`. `reset()` clears it.
- **Plain arithmetic throughout,** no libm (the damper's and Time's values come through
  `exp2Det`), so the delay is bit-exact across platforms.

### 6.5 The global limiter (I23)

After the reverb's return is added and before the mix with the dry signal (§7.2): the wet side
alone is limited; the FX's dry path stays untouched and unclamped (ruling M1-q). **At Off the
output expression is M1's, association included:** `out = (d·(1 − m) + wet'·m) + softLimit(a·r)`
in the FX, exactly as `Processor::render` writes it today, so the three Space FX goldens keep
their bits; only Soft and Peak form the wet side `side = wet'·m + softLimit(a·r)` first and add
the limited side to the dry (a re-association the user opted into by switching the mode on).

Three globals (scope 0, `0x65`–`0x67`): `Limiter Mode` (stepped **Off, Soft, Peak**, default
Off), `Limiter Ceiling` (−24 … 0 dB, default −1 dB) and `Limiter Release` (10 ms–1 s, log,
default 100 ms).

- **Off** is the identity: the sample passes bit for bit. **The per-layer soft limiters stay**
  (M1's law, `softLimit`, per layer before the morph weight), so at Off the only limiting is
  M1's: "the default law equals M1's soft limiter" (programme design §7.7), and no golden moves.
- **Soft** applies M1's `softLimit` to the wet sum, scaled to the Ceiling: `C × softLimit(x / C)`
  with `C` the ceiling's linear gain, so four hot layers whose sum passes 1 are brought below the
  ceiling with M1's knee.
- **Peak** is D5's stronger mode. Under option 1: a zero-lookahead peak limiter: per sample the
  gain is `g[t] = min(g_rel[t], C / |x[t]|)` (1 when |x| ≤ C), where `g_rel` releases from the
  previous gain toward 1 with the Release time constant (its per-sample coefficient from
  `exp2Det` at the grid); the output is `g[t] × x[t]`, never above C in magnitude. No latency,
  plain arithmetic, bit-exact across platforms; a single-sample peak is clamped rather than
  anticipated.
- **A mode change crossfades** over the 10 ms ramp between the outgoing and the incoming law's
  outputs (both computed through the fade), so switching never clicks; Ceiling and Release ramp
  like Mix. **Amended (the final review's fix wave):** where the ramps jump (a new instance, a
  loaded state, a macro or route that moves the mode, a reset) the mode snaps instead, so a loaded
  Peak state is limited from its first sample.
- The instrument's and the FX's tails are unchanged (R-T7a): the FX's infinite, the instrument's
  122.5 s, a constant (D7 names the delay's consequence).

### 6.6 The chain, as built

Per piece, extending wave 1's exact form (INTENT §14, "Layers and the chain"); new stages marked:

```
per layer L (A..D), one task:
   ring_L[t]  = sanitize(in[t]) + fb_L(t) · limited_L[t − 256]
   spectral_L = Spectral kind ? smear(material_L, Position_L) : 0      (one sample per sample)   [new]
   b1_L, b2_L = Σ grains_L by bus, each in spawn order                                           [new]
   w_L        = F1_L(b1_L) ⊕ F2_L(b2_L)     (⊕: bus 2 added only once a grain has been on it;    [new]
                                             F: the bus's filter, or the identity when Off)
   w'_L       = δ_L > 0 ? w_L + δ_L·(diffuse_L(w_L) − w_L) : w_L
   limited_L  = softLimit(w'_L · g_L)
   wet_L      = limited_L · weight_L(t)
instance, after the tasks join:
   wet   = wet_A ⊕ wet_B ⊕ wet_C ⊕ wet_D
   wet'  = wet [+ Amount · delayRead]        (added only where Amount's ramp ≠ 0)                [new]
   FX:          rIn = m·wet' + s·d           instrument:  rIn = wet'
   limiter Off (default), M1's expression and association:
     FX:        out = (d·(1 − m) + wet'·m) [+ softLimit(a·r)]      instrument: out = wet' [+ softLimit(a·r)]
   limiter Soft or Peak:                                                                          [new]
     FX:        out = d·(1 − m) + limit(wet'·m [+ softLimit(a·r)])  instrument: out = limit(wet' [+ softLimit(a·r)])
                                             └─► the resample tap (2) reads out, as §7.2 says
```

With Smear 0, Bus Split 0, both filters Off, the kind not Spectral, Amount 0 and the limiter Off,
every marked line is the identity on the same bits, and the chain is wave 1's.

### 6.7 Parameters: the reserved block (programme design §7.1)

Forty parameters, each `AUTOMATABLE | MODULATABLE`, the stepped ones also `STEPPED`, inside the
`0x60`–`0x7F` blocks; the next-free constants are unchanged (`0x31`, `0x22`), as ruling FR-1
arranged. The layer rows are one list, `GG_LAYER_PARAMS_M2_EXTREMES`, expanded wherever
`GG_LAYER_PARAMS_M2` is (layer A's slots and IDs, `GG_LAYER_PARAMS`), after sub-project 3's list
so the index column stays ascending (`validIndexColumn`); `kLayerParamCount` and its
static_asserts follow the lists. **The forty (name, ID)s are what is pinned** (in
`tests/support/ParamTable.h`), not a total: the count after the rebase includes sub-projects 3's
and 2's parameters (on this branch alone it is 258, `kParamCount`). **Amended, as built:** 258 is 218 (sub-project
1's) plus these forty, as `tests/plugin/params_tests.cpp` checks; the pinned table is the forty,
and the total changes with each later sub-project's parameters.

**Layer scope** (index, in scopes 1–4; displayed with the layer's letter, "A Smear" … "D Bus 2
Resonance"):

| Index | Name | Range | Default |
|---|---|---|---|
| `60` | Smear | 0–100% | 0% |
| `61` | Bus Split | 0–100% (the share of grains on bus 2) | 0% |
| `62` | Bus 1 Filter | stepped: Off, Low-pass, High-pass, Band-pass, Notch | Off |
| `63` | Bus 1 Cutoff | 20 Hz–20 kHz, log | 1 kHz |
| `64` | Bus 1 Resonance | 0–100% | 0% |
| `65` | Bus 2 Filter | as Bus 1 Filter | Off |
| `66` | Bus 2 Cutoff | as Bus 1 Cutoff | 1 kHz |
| `67` | Bus 2 Resonance | as Bus 1 Resonance | 0% |

`0x68`–`0x7F` stay unused in every layer scope.

**Scope 0 (global):**

| ID | Name | Range | Default |
|---|---|---|---|
| `0x000060` | Delay Amount | 0–100% | 0% |
| `0x000061` | Delay Time | 1 ms–2 s, log | 375 ms |
| `0x000062` | Delay Feedback | 0–100% (D7) | 40% |
| `0x000063` | Delay Damping | 0–100% | 30% |
| `0x000064` | Delay Ping-pong | stepped off/on | off |
| `0x000065` | Limiter Mode | stepped: Off, Soft, Peak | Off |
| `0x000066` | Limiter Ceiling | −24 … 0 dB | −1 dB |
| `0x000067` | Limiter Release | 10 ms–1 s, log | 100 ms |

`0x68`–`0x7F` stay unused in scope 0.

- **No metadata differs between the products** for these forty.
- **The patch** stays at version 2: a state that doesn't mention one loads it at its default (a
  literal wave-1 state is decoded in a test and renders M1's bytes). The container is unchanged.
- **The slot order,** as FR-1's X-macro works: each layer's eight new rows join that layer's run
  (A's after `Step16` in A's run, B's in B's, and so on), then the eight globals after `Level D`.
  The AUv2 order is ascending ID regardless (INTENT §18). The slot order isn't frozen; the IDs
  are.

### 6.8 Remote controls and the plugins

- **Five new remote-controls pages** (page IDs 8 to 12; the existing eight keep theirs): **"FX"**
  (Delay Amount, Time, Feedback, Damping, Ping-pong, Limiter Mode, Ceiling, Release) and
  **"Filter A"** to **"Filter D"** (that layer's Smear, Bus Split, Bus 1 Filter, Cutoff, Resonance,
  Bus 2 Filter, Cutoff, Resonance). Thirteen pages.
- **`CLAP_PARAM_RESCAN_TEXT`** is requested, as wave 1 does for a kind change, when a Spectral
  layer's material changes (a sample loaded into or cleared from it), so Position's text follows.
- The plugin's loader and `MEDI` are untouched: the Spectral kind reads whatever sample the layer
  holds (layer A's from the loader until sub-project 2; any layer's from `gg-render`).

### 6.9 `gg-render`

No new keys. `"A Source": 3` (or `"B Source"` …) selects the Spectral kind; the layer's material
is its `sample`/`samples` entry when given, else the render's input ring (D1 option 1), and the
`RenderSpec.h` comment says so beside the Sample rule. The forty parameters are addressed by their
displayed names in `params` and `automation`, and as targets in `mod`. The event line is unchanged
(a Spectral grain is a ring grain; its `pos` is in the spectral ring's time, as a live grain's is
in its ring's).

### 6.10 `gg-bench` and INTENT §22's measurement

Report-only beside X, in the default case list so CI's legs time them and the close can merge
their baseline entries after three full runs (programme design §7.6):

- **`engine-sample-smear`:** `engine-sample` at Smear 1: the two extra draws per spawn and the
  jitter's reads (X's overlap is already far above 32, so the density floor doesn't bind).
- **`engine-buses`:** `engine-sample` with Bus Split 50%: the bus draw and two accumulators.
- **`engine-spectral`:** X's stream over the Spectral kind, the STFT running on the same noise as
  material: the source's cost plus the live kernel's reads, per grain-sample.
- **`gg-bench --chain`:** the wet chain's new per-sample stages, two bus filters (Low-pass, at
  1 kHz, Resonance 50%), the delay (ping-pong, Feedback 50%, Damping 30%) and the limiter (Peak),
  on stereo noise at 48 kHz, as `--space` times the diffuser and reverb: median, min and IQR of ns
  per stereo sample and the % of one core, its own JSON, no ratio and no verdict. `--chain` takes
  `--space`'s options alone.
- **The §22 measurement:** a bench-only kernel, `tools/gg-bench/GrainFilterKernel.h` (not
  `Kernels.h`, which is frozen), is `hermite-simd` plus one `CytomicSVF` per grain stepped per
  output sample (a recursive filter can't vectorize over time, so it is a scalar step per lane per
  sample, the state four floats per channel in the grain's row). It runs as the case
  `hermite-simd-grainfilter` over the M0 workload, and the spec's answer to §22 is its cost over
  `hermite-simd`'s on the M5 Max and on each CI leg: if a per-grain filter costs more than a
  quarter of X per grain-sample, buses with channel masks are v1's answer and per-grain filters
  wait for M5's budget weights, as §14 says; if it costs less, the §23 row says so and M5's
  decision starts from the number. Either way the row records the figures.

`engine-sample` is unchanged in definition and setup (one bus, Smear 0, Sample kind): X keeps its
definition. The `reference` kernel and `Kernels.h` are untouched.

## 7. Determinism and real-time safety

- **Every new draw is counter-keyed:** `Field::SmearJitter`, `SmearDetune`, `Bus` and
  `SpectralPhase` are appended to `rng::Field` (their numbers assigned at the rebase, after 3's and
  2's appends, in merge order; this sub-project's goldens are rendered after that). Nothing else
  is random: the filters, the delay and the limiter have no RNG; pffft has none.
- **Block-size invariance:** Smear's fields are latched per grain; the STFT's hops are absolute
  grid boundaries and its output is written per sample; the filters' coefficients change at grid
  boundaries and interpolate per sample from them; the delay's ramps and glide and the limiter's
  gain are functions of the absolute sample. The `[determinism]` renders gain cases with every
  stage active and automated.
- **Thread-pool invariance:** the buses, the filters and the Spectral source run inside the
  layer's task, on its own state alone; the delay and the limiter run in the serial tail after the
  join. The stub-pool test renders the new cases with 1, 2 and 8 workers and none.
- **Real time:** every buffer (the spectral ring, the STFT's, the delay line, the filters) and
  pffft's setup are allocated with the `Processor` or the `Engine`; `process()`, the tasks and
  `params.flush` allocate nothing. The `[rt]` tripwire and RTSan cover a kind switch to and from
  Spectral with the STFT running, filter type changes mid-stream, Bus Split, Delay Time glides
  and the limiter's modes.
- **Bounded:** the delay loop runs through `softLimit` (below 1 at Feedback 100%); the limiter's
  output never passes its ceiling in Peak and is M1's law in Soft; the Spectral source's output is
  the material's magnitude with the synthesis scaling, clamped on the way into its ring as any
  ring input is (±2^20, non-finite as 0).
- **0- and 1-sample blocks** work as in wave 1: no stage needs more than one sample at a time; the
  STFT's hop fires on the clock, not on the block.
- **Cross-platform:** the event log stays exact (no field is measured from audio); the filters,
  delay and limiter are plain arithmetic and expected bit-exact; the Spectral audio differs in
  its last bits across SIMD flavours, covered by the metric tolerances, as Reverb 2 is.

## 8. Decisions for the director

Each a forced choice; the recommended option is first, with one line on what each costs. Nothing
engineering-only is here (that is §9).

**The director's answers, 2026-10-30, relayed by the programme manager:** every recommended
option. D1 option 1, D2 option 1, D3 option 1, D4 option 1, D5 option 1, D6 option 1, D7 option 1,
D8 option 1. Each is marked below and applied in §6 as written there (§6 was drafted around the
recommended options; the alternatives' text in §6.1, §6.2 and §6.3 is now moot).

**D1. What the Spectral Smear source smears** (§6.2). **Decided: option 1 (2026-10-30).**
1. **The layer's sample when it holds one, otherwise its live ring:** in the instrument a sample
   (or the sidechain on a layer without one), in the FX the input (or a sample `gg-render` or a
   later loader gives the layer). *Cost:* what a Spectral layer smears changes when a sample is
   loaded into it or cleared; Position's text follows.
2. *Always the layer's live ring* (the FX's input, the instrument's sidechain). *Cost:* a sample
   can't be smeared; an instrument's Spectral layer is silent without sidechain audio.
3. *The product's default material* (the FX's ring, the instrument's sample), like the unbuilt
   kinds' fallback. *Cost:* an FX layer can't smear a sample it holds, and an instrument layer
   can't smear its sidechain.

**D2. Freeze on a Spectral layer** (§6.2). **Decided: option 1 (2026-10-30).**
1. **Freeze holds the material, not the smear:** the layer's ring freezes as on a Live layer, so
   the material under the window stands still while the smear keeps evolving with new phases: a
   spectral freeze; Position still scans the frozen material. *Cost:* a frozen Spectral layer never
   repeats itself exactly, so "freeze" here never means "loop".
2. *Freeze also freezes the smear's own ring,* so the grains read a held 5 s of smear and the
   texture loops exactly. *Cost:* a 5 s loop becomes audible as a loop; the evolving smear stops.

**D3. How grains are split across the two buses** (§6.3). **Decided: option 1 (2026-10-30).**
1. **By chance per grain:** Bus Split is the share of grains on bus 2, each grain's bus drawn from
   the counter RNG at spawn; 0 is M1's one bus. *Cost:* at 50% which bus a given grain takes is
   the seed's business, so two seeds filter a cloud differently.
2. *Alternating:* with Bus Split on, even grains take bus 1 and odd grains bus 2 (a switch, not a
   share). *Cost:* at Distribution 0 the buses are two exact half-rate streams, which can read as
   two rhythms; no in-between shares.
3. *By pan side:* grains panned left of a threshold (Bus Split) take bus 1, the rest bus 2.
   *Cost:* with Stereo at 0 every grain is centred and one bus is empty; the split follows Stereo.

**D4. The bus filter's model** (§6.3). **Decided: option 1 (2026-10-30).**
1. **A 12 dB/octave state-variable filter** (sst-filters' `CytomicSVF`): Off, Low-pass, High-pass,
   Band-pass, Notch, with Resonance; smooth under modulation, cheap (16 per instance), libm-free.
   *Cost:* a gentle slope; no self-oscillation or saturation character.
2. *A 24 dB/octave cascade* (two SVF stages in series, `CytomicSVFCascade`). *Cost:* twice the
   cost; a steeper, more synth-like slope; the resonance peak narrows.
3. *A ladder* (sst-filters' `VintageLadders` or `DiodeLadder`): warmer, nonlinear. *Cost:* roughly
   four to eight times the cost, its tanh stages use libm (cross-platform last-bit differences, as
   Reverb 2), and a single model for every type.

**D5. The limiter's stronger mode, "Peak"** (§6.5). **Decided: option 1 (2026-10-30).**
1. **A zero-lookahead peak limiter:** instant attack to the Ceiling, a Release time; no latency,
   plain arithmetic. *Cost:* a single-sample peak is clamped rather than anticipated, so a hard
   transient is flattened for that sample instead of ducked ahead of it.
2. *A hard-knee clipper at the Ceiling* (a waveshaper with a short knee, stateless). *Cost:* every
   excursion past the ceiling distorts rather than rides the gain; no Release.
3. *A look-ahead limiter* (2 ms) with its latency reported to the host. *Cost:* 2 ms of latency in
   every mode, since VST3 and AU hosts restart the plugin on a latency change; every golden moves
   once; the FX's dry path would have to be delayed too.

**D6. How Delay Time changes** (§6.4). **Decided: option 1 (2026-10-30).**
1. **It glides,** the read position moving toward the new time at no more than half a sample per
   sample (Space's Pre-delay rule), so a move bends pitch like tape and never clicks. *Cost:* a
   large jump takes up to twice its size to arrive, bending pitch meanwhile.
2. *It crossfades* from the old time to the new over 50 ms. *Cost:* a brief double echo on every
   change; two reads per sample during the fade.
3. *It jumps* at the next grid cell, as Reverb Size does. *Cost:* a click or glitch on every
   move, as Size's accepted artefact (row 217).

**D7. Delay Feedback's top, and the instrument's tail** (§6.4, §6.5). **Decided: option 1 (2026-10-30).**
**Amended by D7a (the director, 2026-10-30, option 2):** the hold slowly darkens, because Damping 0
keeps a 20 kHz one-pole damper in the loop (a noise burst loses about 2 dB in the 2–4 kHz band
over 20 s; a sine burst holds within 0.1 dB); the bypass option was declined.
1. **100%, the loop soft-clipped,** so the delay can hold a repeat, as Decay ∞ does (slowly
   darkening bright material, per D7a above); the instrument's reported tail stays its constant
   122.5 s (R-T7a). *Cost:* a host that suspends an instrument after its reported tail cuts a
   held delay 122.5 s after the last note; the FX, whose tail is infinite, is unaffected.
2. *100%, and the instrument's tail becomes infinite too* (`CLAP_TAIL_INFINITE` in both products).
   *Cost:* the one R-T7a avoided: hosts that render the reported tail bounce the instrument
   endlessly, and it never sleeps.
3. *95% at most,* so every tail ends. *Cost:* no dub hold; the instrument's constant tail grows to
   about five minutes (a 95% loop at 2 s decays 60 dB in about 270 s), with the same suspension
   trade-off further out.

**D8. How strong Smear is at 100%** (§6.1). **Decided: option 1 (2026-10-30).**
1. **Full:** the overlap is raised toward 32, the jitter is ±half a grain's length, the detune
   ±50 cents. *Cost:* at Smear 1 a tonal source chorusses audibly (±50 cents), and the raised
   density costs CPU where the layer's own overlap was low (never above the 400 Hz ceiling).
2. *Mild:* toward 16, ±a quarter length, ±20 cents. *Cost:* less decorrelation, so at 1000× some
   comb may remain on the most periodic material; the pack renders both for the close's ears.

## 9. My calls

Engineering choices, each with its reason.

- **The Spectral source writes a `LiveSource` of its own and its grains are ring grains** (§5):
  no new kernel family, every tier for free, the admission clamp and head-proximity fade reused,
  and §8.3's uniform interface kept.
- **The STFT is fixed:** N nearest 85 ms as a power of two, hop N/4, Hann in and out, full
  random phases. paulstretch's character is in those choices, and a Window parameter would need
  a hop that stays a multiple of 32 at every value; the layer's Position, Spray and Freeze are its
  controls.
- **Position addresses the material and Spray spreads the grains over the smear** (§6.2): a smear
  is stationary, so where in it a grain reads matters little, while where in the material the
  window sits is the whole instrument. One knob keeps its M1 meaning; nothing new is needed.
- **The spectral ring is 5 s, written every piece, silence while the kind isn't Spectral:** a
  kind switch on the audio thread may not allocate, and a ring written continuously keeps its
  time contiguous (the `LiveSource` contract), so a switch back finds a valid ring; the cost of a
  non-Spectral layer is a write of zeros.
- **Feedback stays 0 on a Spectral layer:** its ring is material, and feeding the smear back into
  it is a later option, not a default surprise.
- **Bus 2 "provably empty" rather than "+ 0", by a sticky flag:** the sign of zero and bit
  identity at rest are what the hash list checks, and a flag that stays up once a grain has been
  on bus 2 never cuts a filter's tail, where a live count would.
- **The Spectral material is decided at the offer, in the `Processor`:** it is the one place that
  knows whether a layer holds a sample, and a material change is then a `CloudParams` re-send like
  a kind change, so the engine never has to guess.
- **`sineDet` moves into the engine's `MathDet.h`,** bit for bit, rather than a second sine in the
  Spectral source: one deterministic sine, and `ModSources` keeps its bits.
- **The limiter's Off path keeps M1's association:** forming the wet side first would move the
  three Space FX goldens with the limiter off.
- **Two filter instances per bus and a 10 ms crossfade on a type change:** an SVF's modes are
  different combinations of the same state, so switching mode in place steps the output; starting
  the new mode from rest under a fade is the same declick rule as the layer on/off ramp.
- **The filters run before the diffuser, inside the task:** the diffuser belongs to the grain
  sound (S1) and should blur the filtered grains; and the task boundary keeps the filters off the
  serial tail.
- **The delay is in-house** (§5): a few dozen lines, bit-exact across platforms, Space's glide rule
  reused; sst-effects' Delay would be mostly disabled plumbing.
- **The delay's return is added only where Amount's ramp isn't 0, and the line is always written:**
  exact identity at rest (the diffuser's and the reverb's precedent), and raising Amount reveals
  the echoes of what was already playing rather than silence.
- **The limiter has Off, Soft and Peak, Off by default,** rather than a Soft default on the mix:
  Soft on the mix would move every golden with a layer past −6 dBFS; Off keeps M1's bytes and Soft
  is one click away for the four-layer case that needs it.
- **The limiter is on the wet side before the mix,** as §7.2 draws it and as M1-q requires of the
  dry path; a user who wants the dry limited puts a limiter after the plugin.
- **The limiter has no lookahead** (whatever D5 picks among 1 and 2): a latency that follows a
  parameter restarts VST3 and AU hosts (R-T7a's lesson), and a constant latency moves every golden
  and delays the dry path for a feature most sessions never switch on.
- **Smear's overlap law is continuous at 0 and capped at 400 Hz:** a discontinuity at the first
  increment would make the knob's bottom a switch, and the Density parameter's ceiling is as far
  as any knob or route may push a layer (the engine's own clamp is the sample rate).
- **Smear's jitter scales with Size, not Spray:** the comb is a function of grain length and
  re-read period; Spray is the user's spread and is often 0 at extreme stretch.
- **Forty parameters, thirteen remote pages, no product differences:** the Edit view (6) is the
  real surface; the pages give a controller the filters and the FX chain before it exists.
- **§22's measurement is a bench-only kernel, not an engine feature:** INTENT §14 already places
  per-grain filters behind M5's budget weights; a number is what §22 asks for, and a prototype
  kernel beside M0's is how this project has always measured a kernel's cost.
- **The new `Field`s are numbered at the rebase:** three sub-projects append to one enum in one
  wave; the merge order fixes the numbers, and this sub-project, merged last, renders its goldens
  after it. Wave 1's and M1's draws are untouched whatever the numbers.

## 10. Verification

**Unit tests** (Catch2, labels in brackets):
- `[engine][smear]`: Smear 0 leaves every field bit for bit (a direct comparison of two engines'
  event streams); at 1 the density is the law's `D_eff`, capped at 400 Hz, the jitter within
  ±J × S, the detune within ±C semitones, the gain the raised overlap's; the comb test (§6.1) at a
  static Position and at 1000×, with its pinned scores and gap.
- `[engine][spectral]`: the hop fires at kH at every block size; the phases are the keyed
  function of (seed, layer, k, b) (two engines with one seed produce one stream; two seeds
  differ); a steady material's RMS holds within 1 dB; the stream is silence while the kind isn't
  Spectral and starts under the margin when it is; the kind switch both ways lets sounding grains
  finish; Freeze freezes the material ring and never the spectral ring (D2 option 1); the sample
  and the ring as material (D1); Position's window clamps at the material's ends.
- `[kernel]`: the SIMD live kernel matches its scalar reference over a spectral ring for every
  `Reader` (`TEMPLATE_TEST_CASE`, which picks up sub-project 3's HQ reader after the rebase).
- `[core][buses]`: Bus Split 0 and both filters Off render the one-bus `Processor` bit for bit
  (a direct comparison); bus 2 is empty until a grain takes it, then the sum is both; each filter
  type's response (a low-pass attenuates a tone an octave above its cutoff by about 12 dB under
  D4 option 1, and so on); the cutoff and resonance ramp per sample (no step above the signal's
  own); a type change crossfades over 10 ms with no click; Resonance 100% is stable on noise for
  60 s.
- `[core][fx]`: the delay at Amount 0 is the identity (the sign of zero included); an impulse's
  echoes land at Time, decaying by Feedback through the damper; Feedback 100% holds within 1 dB
  for 60 s and stays below 1; ping-pong alternates channels; Time's glide bends at most an octave
  down or a fifth up and never clicks; the limiter at Off is the identity, Soft is M1's law at the
  ceiling, Peak never passes the ceiling and releases with its time constant; a mode change
  crossfades without a click.
- `[core][patch]`: the forty (name, ID)s pinned; the next-free constants unchanged; a literal
  wave-1 state loads them at their defaults and renders the same bytes; the new targets' route
  parsing (every wave-2 layer parameter admitted, every modulator's control refused, the new
  globals taking no layer).
- `[determinism]`: block-size invariance (1, 32, 64, 511, 4096, random splits) of FX and
  instrument renders with Smear, Spectral layers, split buses with automated filters, the delay
  gliding and the limiter in each mode; the thread-pool invariance test extended to them.
- `[rt]` and `[rtsan]`: §7's list under the tripwire and RTSan.
- `[plugin]`: the forty new parameters with their pinned IDs; the five new remote pages; `process` and
  `params.flush` under the tripwire with every stage active; the Spectral kind through the CLAP
  factory in both products; `CLAP_PARAM_RESCAN_TEXT` on a material change.
- `[render]`: the Spectral kind's material rule from a spec (with and without a layer sample);
  the forty names parse in `params`, `automation` and `mod`.
- `[bench]`: `engine-sample-smear`, `engine-buses` and `engine-spectral` build and run `--quick`;
  `--chain` runs and writes its JSON; `hermite-simd-grainfilter` runs; `--chain` with an option it
  doesn't take exits 2.
- `[golden][metrics]`: every new case catches all sixteen injected steps or is in the table with
  its count and reason.

**Goldens** (9 new, 45 in all; every WAV of the 36 existing cases keeps its SHA-256, stated per
commit; each new case's spectrogram looked at before it is committed):

| Case | Mode | What |
|---|---|---|
| `inst-smear` | instrument | one held note over noise at a static Position, Size 200 ms, Density 20 Hz (overlap 4): Smear stepped 0, 0.25, 0.5, 1 at 1.5 s intervals |
| `inst-stretch-smear` | instrument | `loop-48k.wav` at 1000× (LFO 1 saw up at 0.01 Hz, depth 0.025, to Position), Size 300 ms, Density 12 Hz: Smear 0 for 4 s, then 1 |
| `inst-spectral` | instrument | layer A Spectral over the generated sweep sample, one held note; Position stepped 0.1, 0.4, 0.7 at 2 s intervals |
| `fx-spectral-freeze` | fx | layer A Spectral over `loop-48k.wav` as input (the ring is the material); Freeze on at 2 s, off at 4 s |
| `fx-bus-filters` | fx | stereo noise, Bus Split 50%; bus 1 Low-pass 300 Hz, Resonance 30%, bus 2 High-pass 3 kHz; bus 1's cutoff automated to 3 kHz at 2 s, bus 2's type to Band-pass at 4 s |
| `inst-filter-lfo` | instrument | **Amended:** one held note over noise (a sweep at a fixed Position is one tone, which hid the filter); bus 1 Low-pass with LFO 1 (0.5 Hz, sine) routed to Bus 1 Cutoff ±2 octaves: a route to a new target |
| `fx-delay` | fx | `loop-48k.wav`; Delay Amount 50%, Time 375 ms, Feedback 50%, Damping 50%, ping-pong on |
| `fx-delay-automate` | fx | a stereo 330 Hz sine; Time 100 ms → 500 ms at 2 s (the glide), Feedback 0 → 90% at 4 s, Amount to 0 at 6 s |
| `fx-limiter` | fx | four Live layers over stereo noise at Level +6 dB each; Limiter Ceiling −6 dB; Mode Off, then Soft at 2 s, Peak at 4 s |

The noise-bed cases (`inst-smear`, `fx-bus-filters`, `fx-limiter`, and `fx-spectral-freeze` where
the smear is dense) will hide some injected steps and are pinned in the click table with their
counts and reasons; `inst-stretch-smear`, `inst-spectral`, `inst-filter-lfo`, `fx-delay` and
`fx-delay-automate` are expected to catch all sixteen. **Amended, as built:** seven cases are
pinned (`inst-smear` 0, `fx-limiter` 0, `fx-bus-filters` 1, `inst-filter-lfo` 13, `fx-delay` 12,
`inst-stretch-smear` 9 and `fx-spectral-freeze` 9); only `inst-spectral` and `fx-delay-automate`
catch all sixteen. `fx-spectral-freeze` also departs: its Position is 0.28, since at 0.4 the
frozen window fell between the loop's bursts. Peak did not click in `fx-limiter` (clean score
4.31), so no table entry is owed to it.

**Benchmarks:** the three engine cases and `--chain` run from `release` with their figures and
the §22 figure in the report; `engine-sample` shows no `regressed` ratio against
`tools/gg-bench/baselines/` on any CI model at the sub-project's end (a regression is fixed here,
programme design §7.6).

**Validators:** clap-validator, pluginval, auval (all three identities) and Steinberg's validator
on `release`, on every CI leg, with the forty new parameters and the five new pages.

**Gate:** `scripts/check.sh` over `dev,asan,rtsan` passes; CI green on the head commit through the
draft PR; a fresh reviewer's final review and its fix wave done; after the rebase onto 3 and 2,
all of it again.

**Exit criteria** (the programme design's §6.4, each with its proof): a 1000× stretch render at
Smear 1 shows no comb peaks above the stated threshold where Smear 0 does (the pinned comb test,
and `inst-stretch-smear`'s spectrogram); the Spectral Smear source is deterministic across
platforms by its event log and within metric tolerances (the `golden` tests on every CI leg);
filters and buses off, delay off and the limiter at its M1 law keep every M1 golden hash
(`scripts/update-goldens.sh`'s list in each commit); new goldens for Smear, Spectral Smear, a
filtered bus and the delay (the table above); §22's row resolved with the measurement (§6.10).

## 11. The listening pack: `docs/listening/m2-extremes/`

Rendered for the record, listened to at sub-project 7 (programme design decision 1). A `README.md`
in the M2 layers pack's form (how to render; a table of files with length, what it is, its peak
and RMS, and what to listen for; a host checklist for the close) and `specs/`:

| File | What | Listen for |
|---|---|---|
| `01-smear-ladder.wav` | one note over noise at a static Position, Smear 0, 0.25, 0.5, 1 | the comb's ringing pitch at 0 fades into an even wash; the level holds |
| `02a-stretch-1000x-smear-0.wav`, `02b-stretch-1000x-smear-1.wav` | the same 1000× stretch of the loop, Smear 0 and Smear 1 (D8's option in `02b`; `02c` at option 2's mild coupling) | A/B: static comb and buzz against a smooth, slowly moving texture; the detune's chorus at 1 |
| `03-spectral-scan.wav` | the instrument, layer A Spectral over the sweep sample, Position swept slowly by LFO 1 | paulstretch's glassy, transient-free smear following the sweep's pitch |
| `04-spectral-freeze-fx.wav` | the FX, layer A Spectral over the loop, Freeze on at 3 s | the bursts' spectrum held as a pad that keeps shimmering (D2) |
| `05-spectral-vs-grains.wav` | layer A Spectral and layer B Sample over one sample, Morph X swept A to B and back | the smear against plain grains of the same material |
| `06-bus-split-filters.wav` | noise, Bus Split 0 → 50% → 100%, bus 1 Low-pass, bus 2 High-pass | one cloud becomes two spectral halves and back; no click at any move |
| `07-filter-type-switch.wav` | a sine cloud, bus 1 stepping Off, Low-pass, Band-pass, High-pass, Notch, Off | each switch crossfades; nothing clicks |
| `08-filter-lfo.wav` | one note, LFO 1 to Bus 1 Cutoff, Resonance 60% | a smooth sweep, no zipper |
| `09-delay-pingpong.wav` | the loop, Time 375 ms, Feedback 60%, ping-pong | echoes alternating sides, darkening with Damping |
| `10-delay-time-glide.wav` | a sine cloud, Time stepped 100 → 500 → 50 ms | the tape bend on each move (D6), no click |
| `11-delay-hold.wav` | a short burst, Feedback 100%, Damping 0 | the repeat holds for 20 s without growing; **Amended (D7a):** a sine burst holds within 0.1 dB, and a noise burst darkens, about 2 dB in 2–4 kHz over 20 s, so the pack uses a sine burst |
| `12-limiter-modes.wav` | four hot layers of noise, Mode Off, Soft, Peak at −6 dB | Off clips nothing and sums hot; Soft rounds it; Peak holds the ceiling with its release |
| `13-all-together.wav` | a Spectral layer and a Smear 0.7 layer, split buses filtered, the delay and Peak | the chain as a whole; does it still sound like one instrument? |

Fourteen renders from thirteen specs (`02` has two or three). **Amended:** `02c` is Smear 0.5, as its README says, because D8's option 2 was a build constant that was never built. The README's host checklist covers:
the "FX" and "Filter A" pages in Bitwig; the Spectral kind in Live with the sidechain feeding an
instrument layer; automating Delay Time and Bus 1 Cutoff in Live; the Peak limiter on a hot
four-layer patch; a Smear 1 patch's CPU against Smear 0 in the host's meter.

## 12. The carry-over

**11 open rows at this sub-project's start** (`docs/m2-carry-over.md`: 51, 85, 87, 88, 144, 148,
184, 217, 218, 221, 232).

**Closed here:** none is owned by this sub-project (programme design §8: 51 is sub-project 3's,
217 and 221 sub-project 6's, the rest upstream, 1.0.0 or M3).

**Touched files with open rows:** `core/include/gg/core/Processor.h` (184: M3's Voice scheduler,
not a when-touched row); `engine/src/Engine.cpp` (51 names its admission lines and
`GrainPool.h`, "with the M1 overload ladder", sub-project 3's feature in this wave, not a
when-touched row). `core/src/space/Reverb2Patched.h` (217, "when Space's FX view arrives") is
**not edited**: the new units are new files beside it. No other open row names a file this
sub-project edits.

**Expected at the end: 11 open rows,** none added. Every minor the final review raises is fixed in
the fix wave unless blocked outside our control or genuinely large (the policy). The global DC
blocker and Feedback on a Spectral layer go to INTENT §22, not to the carry-over: they are design
questions, not open defects.

## 13. INTENT changes (Part II, with one §23 row in the same change)

- **§14:** "Extreme stretch" as built: Smear's coupling law (§6.1) and the Spectral Smear source
  (§6.2: the STFT, the phases, the output ring, the material rule D1, Freeze D2); "Units": the two
  buses per layer with their filters (D3, D4) and the channel mask as a bus bit; "Mix and global
  FX": the delay (D6, D7) and the limiter (D5) as built, the chain of §6.6; the Source kind's
  Spectral built in both products; the memory figure; Feedback 0 on a Spectral layer; the mod
  matrix's target rule restated as "any layer parameter but a modulator's control".
- **§15:** pffft now also in the engine (the Spectral source); sst-filters' `CytomicSVF` in use
  (the first sst-filters unit), compiled in `gg-core`'s own sources under `-ffp-contract=off`.
- **§16:** `engine-sample-smear`, `engine-buses`, `engine-spectral` and `--chain` named beside X as
  report-only; the §22 figure.
- **§17:** the five remote-controls pages; the Filter panel's and FX view's parameters exist,
  headless, for sub-project 6.
- **§18:** the forty parameters (§6.7) in the reserved blocks; thirteen pages; the patch unchanged
  at version 2.
- **§19:** the nine goldens; the comb test; the click table's new entries.
- **§22:** the buses row resolved with the measurement and its figures (§6.10); two new rows,
  post-M2: a global DC blocker (nothing needs it yet), and Feedback into a Spectral layer's
  material.
- **§23:** one row, taking the next free number at the merge (the programme manager's note), for
  D1–D8 with their dates, the rulings made during the build, and the §9 calls that amend Part II (the
  Spectral kind's Position, Spray, Freeze and Feedback rules; the limiter's Off default; the
  spectral ring's size; the Field numbering at the rebase).

Part I is unchanged.

## 14. Risks

| Risk | Mitigation |
|---|---|
| Three sub-projects append to `Rng.h`, `CloudParams`, `GrainSpec` and `Engine` in one wave | Additions only, each a few lines; this sub-project merges last and resolves them at its rebase, then re-renders its goldens and re-runs the gate (§4) |
| Sub-project 3's budget manager changes admission under the raised density | Smear's density is an ordinary `densityHz` to the scheduler; the ladder treats it as any other; the comb test runs after the rebase |
| The STFT's cost or memory surprises | `engine-spectral` and the memory line in §6.2; the ring is 5 s, not 16 |
| The spectral smear sounds different across platforms beyond the tolerances | The metrics' tolerances were set for Reverb 2's last-bit drift; a failure widens nothing silently: the case is examined and the reason recorded |
| A filter setting is unstable | Resonance's map is bounded by a 60 s noise test at 100% on every type |
| `engine-sample` regresses from the per-grain bus dispatch | The dispatch is one byte compare per grain per block; the ratio is checked at the end and fixed here if it moves |
| The comb metric's thresholds are fragile across platforms | The test renders in process on each platform with its own pinned scores at a 6 dB gap; the cepstral peak is a robust statistic of a comb |
| Forty more parameters clutter hosts | Modules per layer, the pages; sub-project 6's panels are the surface |
| The per-grain filter spike eats time | One plan task, bench-only, with a timebox; its number is the deliverable, not a kernel |

## 15. Left to the builder

- Resonance's map onto the SVF's damping, and the exact per-type response figures the tests pin.
- The comb test's pinned scores (within §6.1's rule: Smear 0 above, Smear 1 below, at least 6 dB
  apart), and the 1 s window and cepstral band details.
- The Spectral source's internal buffer layout and how the material window is read from a ring
  (the frame range it may touch behind the head, at least 256 frames) and from a sample (clamped
  at its ends), and the exact fade-in at a switch to Spectral.
- The delay's interpolation at the fractional read (linear, as Pre-delay's) and the damper's
  cutoff map; the ping-pong's exact routing with a mono output.
- The limiter's release arithmetic and the mode crossfade's form.
- How `Engine::process` takes the second bus (an overload, or an output struct), keeping the
  one-bus call M1's path.
- The bench-only grain-filter kernel's exact form, so long as it is `hermite-simd` plus one SVF
  per grain and lives outside `Kernels.h`.
- The listening specs' exact values, voiced so each file makes its point audible.
- The remote pages' slot order within each page.
- Keeping sst-filters out of `Processor.h`, a public header, since `gg-core` links it `PRIVATE`
  (the reverb's pattern, a pointer to an internal unit).
- The filters' grid cells counted on the absolute clock (`t % 32`), across pieces, since a piece
  may end mid-cell.
