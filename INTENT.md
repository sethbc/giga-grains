# Giga Grains — Intent

> The open, CLAP-native granular instrument and effect, at giga scale.

| | |
|---|---|
| **Status** | Intent v1 · 2026-10-26 · pre-M0 (no code yet) |
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
  instance, with a published target (§16).
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
3. The published grain-count and CPU target (§16) is met on reference hardware.
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
  - Clocks: free-running Hz (0.01 Hz to about 4 kHz), tempo divisions, MIDI pitch-sync, external
    or transient trigger.
  - A distribution control runs continuously from 0 (synchronous) to 1 (random, Poisson).
  - Onsets come from a hashed lattice and have sub-sample fractional timing.
- **Grain pool.** Structure-of-arrays storage, preallocated in `activate()`.
  - Defaults to 4,096 grains, configurable up to 16,384.
  - Per-layer and per-voice quotas.
  - Dense index arrays with swap-remove compaction.
- **Kernels.** "Grain-major" SIMD kernels, vectorized over time within each grain. Templated on
  channel count × interpolation tier × source kind.
  - Tiers: **Eco** = linear, **Normal** = 4-point Hermite (the default), **HQ** = 12- to 16-tap
    windowed sinc.
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
- Continuous controls (layer filters, mix) update at block rate.
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
- The control-rate sub-grid is aligned to absolute sample positions.
- No fast-math reductions in the audio path.
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
templates are in `shortcircuit-xt/src/clients/clap-first/CMakeLists.txt`, `libs/CMakeLists.txt`
and `src/scxt-plugin/CMakeLists.txt`.

```
engine/   framework-free, real-time-safe, headless C++ — also powers gg-render
plugin/   CLAP implementation: params, notes/MPE, state, UI↔audio queues
ui/       JUCE editor (GrainCanvas behind a renderer interface)
clients/  CLAP entry points + clap-wrapper targets
```

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
  - Start on **JUCE 8.0.16-security**, which is what Shortcircuit XT ships.
  - M0 tries 9.0.4. JUCE 9 still has no native CLAP support, which confirms the CLAP-first choice.
- **Planned upstream contributions to clap-wrapper:** AUv2 tail support, AUv2 MPE support, VST3
  process-context requirements, a monotonic steady time, and passing AU offline-render state
  through.

**Dependencies.** Pin exact SHAs. The sst libraries have no tagged releases.

| Library | License | Use |
|---|---|---|
| free-audio/clap, clap-helpers, clap-wrapper | MIT | Plugin API and format wrappers |
| VST3 SDK / AudioUnitSDK | MIT / Apache-2.0 | Fetched by clap-wrapper |
| JUCE (GUI modules only) | AGPLv3 / commercial | Editor, widgets, accessibility |
| sst-basic-blocks | GPL-3.0-or-later | SIMD wrappers, sinc tables, LFOs, envelopes, mod matrix |
| sst-filters, sst-waveshapers, sst-effects | GPL-3.0-or-later | Filters, drive, reverb and delay |
| sst-voicemanager | MIT | MIDI1, MPE and CLAP note-ID voice management |
| sst-cpputils, sst-plugininfra, sst-clap-helpers, sst-jucegui | MIT | Real-time utilities, versioned patch state, JUCE shim, widgets |
| simde | MIT | SSE intrinsics on ARM/NEON |
| dr_libs (dr_wav / dr_flac / dr_mp3) | Unlicense / MIT-0 | Audio file decoding |
| libFLAC | BSD-3-Clause | Encoding embedded captures |
| r8brain-free-src (upstream 7.x) | MIT | Sample-rate conversion on load |
| pffft | BSD-like | FFT for Spectral Smear |
| signalsmith-stretch | MIT | Transparent pitch/formant shift in the FX only (not extreme stretch) |
| readerwriterqueue or choc | BSD / ISC | Lossless SPSC queues |
| MTS-ESP client, tuning-library | 0BSD-style / MIT | Microtuning |
| fmt, Catch2 | MIT / BSL-1.0 | Formatting, tests |

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

*These are provisional. The M0 kernel benchmark will confirm or revise them before they become
release gates.*

- **Headline target:** **at least 2,000 Normal-quality grains per instance at 48 kHz, using less
  than 25% of one Apple M1 performance core.**
- **First-principles cost estimates:**
  - about 1 to 1.5 ns per stereo grain-sample for linear interpolation;
  - about 2 ns for Hermite;
  - about 7 to 13 ns for sinc.
  
  Gather operations and cache misses could cost 2× to 4× more than this.
- **Reference hardware:**
  - The floor is GitHub's M1 CI runners plus a mid-range x86 AVX2 machine.
  - The director's M5 Max is the ceiling, not the target.
- **UI budget:** at most 3 ms of UI-thread time per frame with 8,000 visible grains.
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
  - A GPU backend gets a timeboxed spike. Candidates are Visage (which ships a CLAP example but
    is desktop-only) and native Metal/GL.
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

| Product | CLAP / VST3 | AU |
|---|---|---|
| Giga Grains | Instrument | `aumu` |
| Giga Grains FX | Audio effect with optional note input | **Both** `aufx` (normal audio inserts) and `aumf` (MIDI). Logic offers an `aumf` only in instrument slots, with audio arriving via side-chain. |

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
- **CPU benchmark gate in CI**, using the targets in §16.
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
| **M0 Foundations** | Repo, CMake, CI matrix (mac/win/linux), `LICENSE` (AGPL-3.0); headless engine skeleton; `gg-render`; test, golden-render, RTSan and benchmark harness; both plugin shells in every format plus standalone; validators in CI. **Spikes:** kernel benchmark (sets §16), JUCE 9 vs 8.0.16, two-descriptor CLAP to AUv2/VST3/standalone, Ableton MIDI routing into an effect, CPU canvas frame budget. | One grain stream renders deterministically. The empty plugins load in the first-class hosts and pass every validator. |
| **M1 Cloud core** | Grain pool and SIMD renderer (Eco/Normal); Cloud scheduler; parameter model; Sample and Live sources with freeze; 1 layer; diffusion/reverb; state save/load; single-lane canvas plus 8 macros | Both plugins make textures. **Listening gate #1.** CPU baseline recorded. |
| **M2 Giga** | 4 layers plus morph; budget manager; grain target met; mipmaps and the HQ sinc tier; minutes-long buffers with persistence; Smear and Spectral Smear; resample/feedback source; full mod matrix; Mutate; factory presets and CC0 samples | Benchmarks pass. **Listening gate #2.** |
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
| CPU estimates aren't measured yet; gathers and cache misses could cost 2× to 4× | M0 benchmark spike before committing to §16; deterministic budget; quality tiers |
| Determinism is fragile: one CPU-adaptive rule, `std::` distribution or thread-order sum silently breaks it | Rules in §14; block-size invariance tests from M0 |
| Multithreading is uneven across formats; there's no workgroup join via CLAP/clap-wrapper | Single-threaded v1; tasks only when the host offers a thread pool |
| Memory blow-up: layers × minutes × mipmaps × instances | RAM cap per instance, shown in the UI; shared sample pool; buffer resolution options |
| Large state chunks slow or crash hosts | 32 MB embed cap; captures folder; async decode |
| Clicks and level pumping at extremes (head crossing, normalizing thousands of grains) | Engine invariants in §14; click detector in the golden tests |
| sst API churn (no releases); clap-wrapper bugs (transport, AU tail) | Pinned SHAs; upstream fixes; host smoke tests |
| JUCE 9 compatibility of the shim | Start on 8.0.16-security; M0 spike |
| Crowded market, including free granular built into DAWs (Granulator III, Bitwig Fragments) | Differentiate on scale, the canvas, openness and portability, not on feature-count parity |
| Open-source granular projects tend to stall | Tight v1 scope around the texture anchor; a published roadmap; verification that lets Claude-led work proceed safely |
| Windows code-signing eligibility: SignPath's free open-source terms may exclude JUCE's dual license | Budget for Azure Artifact Signing (about $9.99/month) or an OV certificate |

### 22. Open questions

| Question | Resolved in |
|---|---|
| Plugin identities: CLAP IDs (proposed `org.sethbc.giga-grains`, `org.sethbc.giga-grains-fx`), AU manufacturer code (at least one uppercase letter) and subtypes. Frozen at first release. | M0 |
| JUCE 9.0.4 vs 8.0.16-security for the GUI shim | M0 spike |
| How Ableton Live routes MIDI into VST3/AU effects (sets how prominent MIDI-driven FX features are) | M0 spike |
| Whether the CPU canvas holds its frame budget, or a GPU backend is needed | M0 spike / M2 |
| Which hosts besides Bitwig accept CLAP thread-pool requests under load | M2 |
| Whether grain buses with channel masks are enough for v1 sound design, or per-grain filters are needed sooner | M2 |
| Logic Pro's MPE and tail behavior with the wrapped AUv2 | M3 |
| Default behavior of rhythmic streams when the transport stops: keep running or hold | M4 |
| An optional "Classic" Clouds-faithful mode using sst-effects' Nimbus | Post-v1 |
| Windows signing route and budget | Before first public release |

### 23. Decision log

All of the following were decided on 2026-10-26, in the founding brainstorm between the director
and Claude. The research behind them covered frameworks, libraries, granular DSP, the competitive
landscape, and host and UI integration. Each area was adversarially fact-checked against primary
sources.

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
- Local templates: `~/src/shortcircuit-xt` (the CLAP-first layout, CI and installers),
  `~/src/surge`, `~/src/OB-Xf`
