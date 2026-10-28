# Giga Grains — Intent

> The open, CLAP-native granular instrument and effect, at giga scale.

| | |
|---|---|
| **Status** | Intent v1 · 2026-10-26 · M0 (Foundations) done 2026-10-26; M1 (Cloud core) in progress: sub-project 1 of 6 (the FX slice) built 2026-10-27 |
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
   Under overload the engine degrades gracefully (it thins grains) and never drops out.
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
  - Compatible: AGPL-3, GPL-3 (combined under GPLv3 §13), MIT, BSD, Apache-2.0, zlib, ISC,
    BSL-1.0, Unlicense.
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
  sample (RAM)      │ Source ─► Scheduler ─► Grain pool ─► Kernels ─► │ ─► grain buses / layer FX
  live ring         │           (when/what)  (SoA, fixed)  (SIMD)     │
  oscillator        └─────────────────────────────────────────────────┘
  resample tap ◄──────────────────────────────────────────────┐
                                                               │
 Layers A–D ─► Mix / XY morph ─► Global FX (diffuse · delay · reverb · limiter) ─┴─► Out
          ▲
 Modulation: macros · LFOs · envelopes · random/S&H · step · MIDI/MPE · env follower · transport
```

**Units**

- **Source.** Owns or generates audio and exposes a batched `read(position, rate)` interface.
  The four kinds are Sample, Live, Oscillator and Resample (§4).
- **Scheduler.** One per grain stream. It decides *when* grains start and *with what* parameters.
  - Modes: **Cloud** (asynchronous; v1), **Sync** (tempo grid), **Voice** (MIDI/MPE notes spawn
    streams), **Pitch-sync** (grain rate tied to pitch, for pulsar and formant sounds).
  - Clocks: free-running Hz (0.01 Hz up to the sample rate; the plugins' Density stops at
    400 Hz), tempo divisions, MIDI pitch-sync, external or transient trigger.
  - A distribution control runs continuously from 0 (synchronous) to 1 (random, Poisson).
  - Onsets follow a recurrence driven by the counter RNG, with sub-sample fractional timing
    (Cloud core rules, below).
- **Grain pool.** Structure-of-arrays storage, preallocated in `activate()`.
  - Defaults to 4,096 grains, configurable up to 16,384.
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
- **Mix and global FX.** Four layers feed a mix with XY morph, then diffusion, delay, reverb and a
  limiter.
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
  rules, below). Mix and Level are also smoothed per sample.
- Grain parameters are **snapshotted when each grain starts**, so each grain gets its own value.

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

- **Loading.**
  - Sources live in RAM. Disk streaming doesn't suit random-access grain reads.
  - Cost is about 23 MB per minute for float32 stereo at 48 kHz.
  - Files are decoded on a background thread, and the memory is touched before use so page faults
    can't stall the audio thread.
  - The buffer is published as an immutable, reference-counted object via a pointer swap, and
    freed later off the audio thread.
- **Memory management.** A process-wide sample pool removes duplicate copies of the same file.
  Each instance has a memory cap, shown in the UI.
- **Live ring buffer.**
  - Each block writes before it reads.
  - An admission clamp keeps grains from reaching the write head.
  - The head-crossing policy is selectable. Default: clamp, with about a 256-sample crossfade.
  - Freeze and overdub use smoothed gains and crossfaded seams.

**Cloud core rules** (M1 sub-project 1, the FX slice; §23 #36)

- **The control grid** is 32 samples, aligned to absolute sample positions.
  - A parameter or modulation event takes effect at the first multiple of 32 at or after its
    absolute timestamp. The last event per parameter wins.
  - A boundary that falls exactly at a call's end applies in the next call (ruling M1-u).
  - Mix and Level ramp linearly to each new target over 10 ms, per sample, as a function of the
    absolute sample. A ramp restarts from its current value only when that parameter's target
    changes (ruling M1-s).
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
- **The wet chain:** Σ grains × Level → soft limiter → mix with dry, `dry × (1 − m) + wet × m`.
  - **Soft limiter:** the identity up to |x| = 0.5 (−6 dBFS). Above, `sign(x) × (1 − 0.25/|x|)`,
    capped below 1, with slope 1 at the knee and no libm; NaN gives 0 (ruling M1-t). It is a
    first, minimal form of the global limiter.
  - **Dry path:** non-finite samples count as 0, and finite samples aren't clamped, so at Mix 0
    a normal float passes bit for bit (ruling M1-q).
  - One `ScopedDenormalsOff` covers the whole processor call, the dry/wet sum included.
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
  - **Freeze is live until toggled** (§23 #40). The ring isn't saved until M2, so a fresh ring
    starts empty. A Freeze that is on when the ring starts, restored with a project, kept
    through a reactivation or kept through a reset, therefore leaves the effect playing live
    grains: Freeze engages only on an off→on transition after the activation or reset, seen at a
    grid boundary or at a block's first sample. The parameter and its saved state still read On
    while it isn't holding. A boundary's events apply as one group, so off and on again at one
    boundary is no transition, and the result doesn't depend on block size.

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

- The seed lives in the plugin state.
- RNG is counter-based (SplitMix64 or Philox style), keyed by (seed, layer, stream, field, grain
  index).
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
  engine RNG.
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
plugin/   CLAP implementation: params, notes/MPE, state, UI↔audio queues
ui/       JUCE editor (GrainCanvas behind a renderer interface)
clients/  CLAP entry points + clap-wrapper targets
```

- **The `core/` layer (`gg-core`, since M1; §23 #38)** sits between the engine and the plugin. It
  holds the patch and its versioned state XML (public API `gg/core/PatchState.h`), the only
  parameter → engine mapping (`ParamMap`), and the `Processor`: the control grid, the Mix and
  Level ramps, the FX's live input, the wet chain and the dry/wet mix. The plugin and `gg-render`
  drive the same `Processor`, so the goldens test what a DAW plays. It has no JUCE.
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
  - Pin a commit that includes the AUv2 transport fix, which so far exists only on the `next`
    branch.
  - Always list `PLUGIN_FORMATS` explicitly, because the default includes AAX.
  - AUv3 comes later. It needs the Xcode generator, one app extension per plugin, and UIKit
    support in the GUI shim.
- **JUCE is used for the GUI only**, attached through sst-clap-helpers' `clap_juce_shim`, with
  sst-jucegui widgets.
  - Start on **JUCE 9.0.4**. The M0 spike built the shim and both plugins against it with no
    source changes, and on macOS arm64 every validator passed (`docs/spikes/m0-juce9.md`,
    §23 #24). CI run 40445561691 then passed on every leg, Windows ClangCL included, with
    JUCE's WebP support off (`JUCE_USE_WEBP=0`; §23 #29). Shortcircuit XT still ships
    8.0.16-security, which remains the fallback.
  - JUCE 9 still has no native CLAP support, which confirms the CLAP-first choice.
- **Planned upstream contributions to clap-wrapper:** AUv2 tail support, AUv2 MPE support, VST3
  process-context requirements, a monotonic steady time, and passing AU offline-render state
  through.

**Dependencies.** Pin exact SHAs. The sst libraries have no tagged releases.

| Library | License | Use |
|---|---|---|
| free-audio/clap, clap-helpers, clap-wrapper | MIT | Plugin API and format wrappers |
| VST3 SDK / AudioUnitSDK | MIT / Apache-2.0 | Fetched by clap-wrapper |
| RtAudio 6.0.1 | MIT-style (adds a non-binding request to send changes upstream) | Fetched by clap-wrapper (CPM tag `6.0.1`, so pinned through clap-wrapper's SHA) for a Linux standalone built without the PipeWire and JACK headers. Its bundled Steinberg ASIO files have no license grant, so ASIO is off and Windows doesn't build RtAudio (§22, §23 #30) |
| RtMidi 6.0.0 | MIT-style (the same terms) | Fetched by clap-wrapper (CPM tag `6.0.0`) only with `CLAP_WRAPPER_STANDALONE_RTAUDIO`, which we leave off |
| wil (Windows Implementation Libraries) | MIT | Fetched by clap-wrapper (CPM tag `v1.0.240803.1`) for the Windows standalone |
| JUCE (GUI modules only) | AGPLv3 / commercial | Editor, widgets, accessibility |
| sst-basic-blocks | GPL-3.0-or-later | SIMD wrappers, sinc tables, LFOs, envelopes, mod matrix |
| sst-filters, sst-waveshapers, sst-effects | GPL-3.0-or-later | Filters, drive, reverb and delay |
| sst-voicemanager | MIT | MIDI1, MPE and CLAP note-ID voice management |
| sst-cpputils, sst-plugininfra, sst-clap-helpers, sst-jucegui | MIT | Real-time utilities, versioned patch state, JUCE shim, widgets |
| tinyxml (bundled in sst-plugininfra) | zlib | The patch state's XML, private to `gg-core` |
| simde | MIT | SSE intrinsics on ARM/NEON |
| dr_libs (dr_wav / dr_flac / dr_mp3) | Unlicense / MIT-0 | Audio file decoding |
| libFLAC | BSD-3-Clause | Encoding embedded captures |
| r8brain-free-src (upstream 7.x) | MIT | Sample-rate conversion on load |
| pffft | BSD-like | FFT for Spectral Smear |
| signalsmith-stretch | MIT | Transparent pitch/formant shift in the FX only (not extreme stretch) |
| readerwriterqueue or choc | BSD / ISC | Lossless SPSC queues |
| MTS-ESP client, tuning-library | 0BSD-style / MIT | Microtuning |
| fmt, Catch2 | MIT / BSL-1.0 | Formatting, tests |
| stb_image_write | public domain / MIT | Spectrogram PNGs |
| nlohmann/json | MIT | Metrics and render-spec JSON |
| CLI11 | BSD-3-Clause | Render CLI (`gg-render`) |

- **sst-plugininfra is in use since M1** (`main` @ `8e4271c`; it has no release tags). Only its
  `patchbase` interface target and its bundled tinyxml are built, linked privately into `gg-core`
  (ruling M1-l), so no other target sees tinyxml or its `-fno-char8_t` flag.
  - Its bundled `libs/asiosdk` is never built (unused). At this pin it is ASIO SDK 2.3.4, whose
    `LICENSE.txt` grants the "Steinberg ASIO License or GPLv3" (§22).
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
- **Queues:**
  - Parameters and commands going from the UI to the audio thread use a lossless,
    non-overwriting SPSC queue.
  - Telemetry going from the audio thread to the UI may use a lossy queue that drops entries when
    full, such as sst-cpputils' ring buffer.

**Threading**

- **One audio thread per instance.** Hosts already parallelize across tracks.
- **Rendering is split into tasks:** layer × grain chunk, each with a private accumulator, summed
  in a fixed order. When the host offers CLAP's thread pool (Bitwig does), the tasks run in
  parallel; otherwise the same tasks run in a loop.
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

*The headline is measured on the reference core at the end of M1 and published as measured
(§23 #39). The M0 kernel benchmark (below) is its starting point.*

- **Headline (measured on the reference core, published as measured):** **2,000 Normal grains
  per instance at 48 kHz in X% of one Apple M5 Max performance core.**
  - X is measured at the end of M1, in sub-project 6, and published as measured. **There is no
    ceiling on X.**
  - The text always names the chip, so nobody reads it as an M1 or x86 figure.
  - The M2 budget manager keeps any grain count safe by degrading deterministically (§14). The
    promise is an honest, reproducible figure, not a threshold.
  - `gg-bench` also times the same case over the live ring; that is published beside X as the
    FX's figure (§23 #44).
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
- **Informative floor figures** (M0 CI, full-length `gg-bench`). CI's legs keep running the
  full-length benchmark, and their figures go in the benchmark doc as informative, not promised.
  The SIMD Hermite kernel, 2,000 grains, at blocks 32 / 64 / 256:
  - **Apple M1 VM** (GitHub `macos-latest`): 5.52 / 5.41 / 5.00 ns, which is 52.9% / 51.9% /
    48.0% of one core.
  - **AMD EPYC 7763, Windows** (ClangCL): 6.22 / 5.93 / 5.72 ns (59.7% / 56.9% / 54.9%).
  - **AMD EPYC 7763, Linux** (gcc 12): 7.21 / 6.62 / 6.26 ns (69.2% / 63.5% / 60.1%).

  That is 2.4× to 3.2× the M5 Max's share for the same grains, which §21 rounds to 2× to 3×. Small blocks also cost
  relatively more on some legs. On Linux, block 32 costs 15% more per grain-sample than block
  256, against 8% on the M5 Max.
- **CI's CPU gate is a regression gate,** measured as ratios within one process (carry-over
  B-I2). It never gates on an absolute headline.
- **Sub-project 2's optimization work is a fixed list of four levers,** each tried, kept if it
  wins, then stop:

  | Lever | What it attacks |
  |---|---|
  | Per-grain software prefetch | the first, likely cache-missing taps of each grain in each block |
  | A stereo-interleaved source layout | two address streams per tap; it halves the per-tap address work |
  | Cache-aware grain scheduling | source locality across grains. It reorders memory fetches only, never the summation |
  | Contiguous vector loads with shuffles in place of the per-lane gather | 8 one-at-a-time tap loads per output sample (NEON `tbl`, SSE shuffles) |

  - **A lever is kept** when its in-process ratio against the current kernel improves beyond the
    run-to-run spread, on the M5 Max or on any CI floor leg, and no leg regresses beyond its
    spread. Judging on the M5 alone would undersell the cache levers, which mainly help older
    machines.
  - **Every lever obeys the determinism rules unchanged** (§14): grains are summed in spawn
    order, so a reordering lever may change only fetch order; the output stays bit-identical at
    any block size; a lever that changes output bits justifies it in a golden-regeneration
    commit.
  - **When all four are tried, optimization stops.** Sub-project 6 measures X and publishes it.
  - **Outcome (sub-project 2, 2026-10-28): one lever kept, three rejected, optimization
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
  - The engine publishes only while the editor is open.
  - Visual thinning never touches the audio RNG.

### 18. State, presets and identities

**Session state** is a versioned, chunked container:
- `PTCH`: the patch, using sst-plugininfra PatchBase with migrations;
- `MEDI`: a media manifest listing each sample's content hash, absolute and library-relative
  paths, format details and an embed flag;
- `AUDI` (optional): embedded audio as FLAC.

**Sample handling**
- **What gets embedded:**
  - User samples are referenced, not embedded.
  - Captures and freezes are embedded up to **32 MB per instance**. Beyond that, they are written
    to a captures folder as FLAC files named by hash, and referenced from the state.
- **Loading:** sessions load quickly and decode audio asynchronously.
- **Missing media:** shows placeholders, with relink and a "collect and save" command.

**Presets.** There is one preset system: CLAP preset discovery. clap-wrapper bridges it to VST3
program lists and AU factory presets.

**Frozen at first release.** Changing any of these breaks users' saved sessions:
- CLAP parameter IDs (new ones are append-only);
- AUv2 parameter order;
- factory preset order;
- plugin identities.

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
  | `0x010001` | Position | 0–16 s behind the write head | 0.25 s |
  | `0x010002` | Size | 5 ms – 2 s | 150 ms |
  | `0x010003` | Density | 0.25–400 grains/s | 16 |
  | `0x010004` | Distribution | 0–100% (regular ↔ random) | 50% |
  | `0x010005` | Pitch | −24 … +24 semitones | 0 |
  | `0x010006` | Spray | 0–2 s | 50 ms |
  | `0x010007` | Stereo | 0–100% per-grain pan spread | 50% |
  | `0x010008` | Freeze | off/on | off |

  - Every one is `AUTOMATABLE` and `MODULATABLE`; Freeze is also `STEPPED`. Modulation adds a
    clamped offset to the base value and is never written back.
  - Both products expose the same list, so they share one state format. In the instrument, Mix
    is `HIDDEN` and has no effect, and its remote-controls slot is empty (ruling M1-w).
  - The CLAP value is the parameter's sst-basic-blocks `ParamMetaData` value, a log-domain value
    for the log ranges. Values are floats end to end.
- **The patch** (the future `PTCH` chunk; today the plugin's whole state) is `gg-core`'s
  versioned XML: patch id
  `org.sethbc.giga-grains.patch`, version 1. Each value also carries its float bits, so a saved
  and reloaded state renders identically, and the engine seed is saved with it. New instances
  start at seed 0.
- **Remote controls:** one 8-slot page, "Cloud": Position, Size, Density, Pitch, Spray, Stereo,
  Freeze, Mix. Sub-project 5 replaces it with the 8 macros.

**CLAP features for v1:**
- `params` with per-note modulation flags
- `note-ports` (CLAP, MIDI and MIDI-MPE dialects)
- `voice-info`
- `state` and `state-context`
- `preset-load`
- `tail`
- `latency` (0)
- `audio-ports` (with an optional sidechain)
- `render`
- `remote-controls` (the macro row)
- `param-indication`
- `track-info`
- `gui`
- `thread-pool` (optional)

### 19. Verification strategy

- **The engine is a headless library.** It builds and tests without any plugin framework.
- **Unit tests (Catch2):**
  - grain onset accuracy
  - window sums
  - interpolators and mipmaps
  - live-buffer head crossing
  - parameter ranges
  - state round-trip and migrations
- **`gg-render` offline CLI.** It takes a preset, an input WAV and a MIDI/automation script, and
  writes a WAV deterministically. It's the backbone of audio testing and how Claude "listens".
- **Golden renders compared by metrics:**
  - spectral distance
  - RMS envelope
  - peak and DC
  - stereo correlation
  - click detection
  - NaN, Inf and denormal checks

  Bit-exact comparison applies on the same platform.
- **Determinism tests.**
  - Block-size invariance: renders at block sizes 1, 32, 64, 511 and 4096, plus random splits,
    must be bit-identical.
  - Grain-event logs are compared across platforms.
- **Fuzz and property tests.** Parameter sweeps, extreme values and bursts of automation must
  produce no NaNs, crashes, runaway gain, or clicks above a threshold.
- **CPU benchmark gate in CI:** a regression gate on in-process ratios, from M1's baseline. The
  §16 headline is a published measurement, not a CI gate. Through M1 the gate reports and never
  fails; sub-project 6 makes it blocking, with the published X.
- **Real-time safety.**
  - Clang RealtimeSanitizer on upstream LLVM 20 or newer (Apple clang doesn't ship it).
  - ASan and UBSan.
  - Tripwires that fail tests on allocation or locking in the audio path.
- **Format validators in CI:** clap-validator, pluginval, auval, and Steinberg's VST3 validator.
- **Machine-readable artifacts.** Spectrogram images and summary statistics, so Claude can inspect
  renders and not just assert pass/fail.
- **A human listening gate at every milestone.**
- **CI and release pipeline.** Adapted from Shortcircuit XT's GitHub Actions and sst-plugininfra
  scripts:
  - macOS: signed and notarized PKG inside a DMG;
  - Windows: Inno Setup installer;
  - Linux: tar archive.

### 20. Roadmap

| Milestone | Delivers | Exit criteria |
|---|---|---|
| **M0 Foundations** (done 2026-10-26) | Repo, CMake, CI matrix (mac/win/linux), `LICENSE` (AGPL-3.0); headless engine skeleton; `gg-render`; test, golden-render, RTSan and benchmark harness; both plugin shells in every format plus standalone; validators in CI. **Spikes:** kernel benchmark (sets §16), JUCE 9 vs 8.0.16, two-descriptor CLAP to AUv2/VST3/standalone, Ableton MIDI routing into an effect, CPU canvas frame budget. | One grain stream renders deterministically. The empty plugins load in the first-class hosts and pass every validator. **Met** (§23 #29). |
| **M1 Cloud core** (starts from `docs/m1-carry-over.md`) | Six sub-projects, each spec → plan → build → review (§23 #35): 1 FX vertical slice (`docs/superpowers/specs/2026-10-27-m1-fx-slice-design.md`); 2 Cloud engine; 3 Sources; 4 Space; 5 Perform UI; 6 Close. Together they deliver: grain pool and SIMD renderer (Eco/Normal; Eco is engine-only until M2, §23 #43), the §16 lever list (§23 #39); Cloud scheduler; parameter model; Sample and Live sources with freeze; note-triggered freeze/capture in the FX (§23 #32); 1 layer; diffusion/reverb; state save/load; single-lane canvas plus 8 macros | Both plugins make textures. **Listening gate #1.** CPU baseline recorded; the CPU gate reports without failing until sub-project 6 makes it blocking (§23 #44). The §16 headline measured on the reference core and published (§23 #39). clap-validator runs in full: M0 skipped its `param-conversions` test, which panics on a plugin with no parameters (ruling T8-c, free-audio/clap-validator#40); sub-project 1 removed the exclusion (§23 #35). |
| **M2 Giga** | 4 layers plus morph; budget manager; no CPU regression vs the M1 measured headline (§23 #41); mipmaps and the HQ sinc tier; minutes-long buffers with persistence; Smear and Spectral Smear; resample/feedback source; full mod matrix; Mutate; factory presets and CC0 samples, including MIDI-driven FX presets (§23 #32) | Benchmarks pass. **Listening gate #2.** |
| **→ v1.0** | M0 to M2 polished | Success criteria (§10) met. |
| **M3 Playable** | Voice scheduler, polyphony, MPE, note expressions, polyphonic modulation, per-voice envelopes, pitch-sync/formant grains, microtuning | Listening gate. First-class hosts pass. |
| **M4 Rhythmic** | Sync scheduler: grid, swing, probability, patterns, transport-stop/loop policies | Listening gate. Sync tests pass under loops and jumps. |
| **M5 Synthesis and extremes** | Oscillator sources, synthetic grains, per-grain filters | Listening gate. Budget weights calibrated. |
| **M6 Reach** | iOS AUv3 and/or LV2, accessibility polish, community infrastructure | Target-specific validation passes. |

Why this order after v1.0:
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
| Large state chunks slow or crash hosts | 32 MB embed cap; captures folder; async decode |
| Clicks and level pumping at extremes (head crossing, normalizing thousands of grains) | Engine invariants in §14; click detector in the golden tests |
| sst API churn (no releases); clap-wrapper bugs (transport, AU tail) | Pinned SHAs; upstream fixes; host smoke tests |
| JUCE 9 compatibility of the shim | The M0 spike passed on macOS arm64, so the project is on 9.0.4 (§23 #24). CI run 40445561691 passed on Windows, Linux and macOS (§23 #29), and the macOS leg now also runs the mac-universal x86_64 slice's tests under Rosetta (§23 #33); 8.0.16-security is the fallback |
| Crowded market, including free granular built into DAWs (Granulator III, Bitwig Fragments) | Differentiate on scale, the canvas, openness and portability, not on feature-count parity |
| Open-source granular projects tend to stall | Tight v1 scope around the texture anchor; a published roadmap; verification that lets Claude-led work proceed safely |
| Windows code-signing eligibility: SignPath's free open-source terms may exclude JUCE's dual license | Budget for Azure Artifact Signing (about $9.99/month) or an OV certificate |

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
| Windows signing route and budget | Before first public release |
| Windows standalone ASIO: it's off, because RtAudio's bundled Steinberg ASIO files have no license grant (§23 #30). Re-enable it only from a Steinberg ASIO SDK release whose license is AGPLv3-compatible (Steinberg's GPLv3 release). Such a release now exists: ASIO SDK 2.3.4, licensed "Steinberg ASIO License or GPLv3", which sst-plugininfra bundles (unused, never built; §15, §23 #38). Whether to re-enable ASIO stays open | Before the first distributed Windows build |

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
| 40 | Freeze is live until toggled | Final-review finding I1 of M1 sub-project 1: Freeze is saved in the state but the live ring isn't (persistence is an M2 non-goal), so a Freeze that was on when a fresh ring started froze that ring's first 20 ms, usually silence, and the effect stayed silent until the user toggled Freeze. That happened on every project reopened with Freeze on, every reactivation and every CLAP `reset()`, which clap-wrapper's VST3 calls on each `setProcessing(false)`. The director chose "live until toggled" over documenting the silence: the `Processor` engages Freeze only on an off→on transition after an activation or reset, evaluated on the control grid; until then the effect plays live grains while Freeze reads On. Once engaged, the 20 ms ramp and the admission clamp (M1-p) are unchanged. With it the effect is always audible after a reload; the cost is that a reopened frozen pad is not held until M2 persists the ring and revisits this. No golden starts frozen, so none changed. Decided 2026-10-27 by the director, during M1 sub-project 1 (final review); see §14 |
| 41 | §20's M2 exit wording after #39 | §20's M2 row still said "grain target met", but #39 removed the CPU target: the §16 headline is measured at the end of M1 and published, with no ceiling, and CI's CPU gate is a regression gate on in-process ratios. The director's wording replaces it: "no CPU regression vs the M1 measured headline". Decided 2026-10-27 by the director, during M1 sub-project 1; see §20 and #39 |
| 42 | Reference templates named by their upstream repositories | Part II and CLAUDE.md named the three reference templates (shortcircuit-xt, surge, OB-Xf) only by local paths, which a reader without those clones can't follow. Each mention of a template now names its GitHub repository under the surge-synthesizer organization, checked against each local clone's `origin`: the "Key sources" list gives one bullet per template, with its local clone next to its URL; §15's CLAP-first paragraph links shortcircuit-xt once, with its template files' paths relative to the repository; CLAUDE.md's "Reference templates" bullet gives the URLs and says a session may read the local clones when they're present. A correction; no decision changes. Decided 2026-10-27 by the director, during M1 sub-project 1; see §15 and "Key sources" below |
| 43 | Sub-project 2's kernel bits and the Eco tier | Rulings 1 and 2 of M1 sub-project 2. **Kernel bits:** the renderer gets new per-sample math, defined once by a scalar reference: the window phase is `e × invDuration`, with `invDuration` latched once, and each channel's gain is one latched product, so a sample adds `read × (w × gain_c)` (on the live ring, `read × ((w × f) × gain_c)`). The 4-wide SIMD kernels match that reference bit for bit on every platform's own build, with no horizontal operations and no FMA. The audio-metric goldens are regenerated once, in one commit that says why; the grain-event logs don't change. **Eco is engine-only until M2:** the tier is latched per grain, at its onset, from `CloudParams::quality`, and stored in the pool; `gg-render`'s sample mode takes an optional `stream.quality`, and a golden case covers Eco. Both plugins stay Normal; a Quality parameter arrives in M2 with HQ and the budget manager. Decided 2026-10-27 by the director, during M1 sub-project 2; see §14 and `docs/superpowers/specs/2026-10-27-m1-cloud-engine-design.md` §2–§3 |
| 44 | The CPU gate is report-only through M1; X is the Sample-mode case, the live figure beside it | Rulings 3 and 4 of M1 sub-project 2. **The gate:** sub-project 2 builds CI's CPU gate (in-process ratios against a frozen reference kernel, per-leg baselines and a verdict), and CI reports the verdict without failing; sub-project 6 makes it blocking, with the published X. **The headline's source:** X is the Sample-mode case, 2,000 Normal grains of 100 ms reading a stereo buffer in RAM; `gg-bench` also times the same case over the live ring, and that figure is published beside X as the FX's. Decided 2026-10-27 by the director, during M1 sub-project 2; see §16, §19, §20 and `docs/superpowers/specs/2026-10-27-m1-cloud-engine-design.md` §2 and §5 |
| 45 | The Cloud density ceiling is the sample rate | Ruling 5 of M1 sub-project 2, which amends ruling M1-a as recorded in #36. The §16 case needs 2,000 overlapping grains of 100 ms, which is 20 kHz at 48 kHz, but the scheduler clamped a positive density to [0.01, min(4000, sampleRate)]. The 4 kHz cap goes: a positive density clamps to [0.01, sampleRate], so onsets average at most one per output sample, as in M0. The plugins' Density parameter still stops at 400 Hz, so nothing a user can reach changes, and no golden changes. §14's clock range, "0.01 Hz to about 4 kHz", becomes "0.01 Hz up to the sample rate". Decided 2026-10-27 by the director, during M1 sub-project 2; see §14 and `docs/superpowers/specs/2026-10-27-m1-cloud-engine-design.md` §2 |
| 46 | §16 levers tried: the interleaved source kept, three rejected | Sub-project 2 tried §16's four levers, each judged by in-process ratios against the kernel before it, on the M5 Max and on CI's three legs. **Kept: the stereo-interleaved source,** both halves. A stereo sample source gets an interleaved copy (L, R per frame), so each SIMD tap is one address; the live ring stores L, R per slot. On the M5 Max the Sample case is 7–9% faster and the live case 9–13%; the live half also wins 8–14% on Windows and loses nowhere. **The director's exception:** the interleaved *sample* half is kept although the Windows leg's sample ratio regressed 1–2% (Eco up to 7% at block 32), which §16's keep rule ("no leg regresses beyond its spread") would reject. The director preferred to keep it for the M5's 7–9% sample win, at 2× memory for a stereo source. **Rejected:** per-grain prefetch (its sample half regressed Eco on the x86 legs; its live half, ported onto the interleaved ring, showed no win on the M5); cache-aware scheduling (11% to 77% slower on every leg); contiguous loads with shuffles (5% to 49% slower on x86; ported to arm64 only, it no longer paid on any path the engine runs, since the interleaved reads are already contiguous). None of the rejected levers is merged; their branches stay for the record. Optimization stops here; sub-project 6 measures X. Decided 2026-10-28 by the director, during M1 sub-project 2; see §16 and `docs/spikes/m1-cpu-levers.md` |
| 47 | CI CPU baselines come from at least three separate runs | Found while judging #46's levers: on CI, ratios for unchanged code drifted between runs far beyond the in-run IQR, up to ~20% on Windows, where the two runs landed on different CPU models (GitHub's hosted x86 pools mix AMD EPYC 7763, EPYC 9V45 and Intel Xeon 8370C), and up to 6% on Linux on the same model. One run's IQR therefore says nothing about the next run, so a baseline needs at least three separate CI runs, its tolerance taken from their spread (`gg-bench --make-baseline`: `max(0.05, 2 × (max − min) / median)`). This supports §19's report-only gate through M1. Before sub-project 6 makes the gate blocking, it must handle the CPU mix, with a tolerance that covers every model or a baseline per model. Recorded 2026-10-28, during M1 sub-project 2; see §16, §19 and `docs/spikes/m1-cpu-levers.md` ("CI variance") |

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
- surge (reference template; GitHub, surge-synthesizer organization; local clone `~/src/surge`):
  https://github.com/surge-synthesizer/surge
- OB-Xf (reference template; GitHub, surge-synthesizer organization; local clone `~/src/OB-Xf`):
  https://github.com/surge-synthesizer/OB-Xf
