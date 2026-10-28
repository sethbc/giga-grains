# M1 sub-project 1: the FX vertical slice — design

| | |
|---|---|
| **Milestone** | M1 Cloud core (INTENT §20), sub-project 1 of 6 |
| **Status** | Approved in brainstorming by the director, 2026-10-27 |
| **Spec authority** | `INTENT.md`; this document records how this sub-project implements it |
| **Starts from** | `main` at `1905b63` (M0 merged) and `docs/m1-carry-over.md` |

## 1. Why this sub-project

M1 is too large for one spec, so it is split into six sub-projects. Each one goes through spec →
plan → build → review:

1. **FX vertical slice** (this document): a playable texture, end to end, early.
2. **Cloud engine:** the SoA grain pool and SIMD Eco/Normal kernels that close the §16 CPU gap,
   and a gate-grade `gg-bench`.
3. **Sources:**
   - the Sample source, which gives the instrument its sound;
   - Live-source depth: note-triggered freeze/capture in the FX (§23 #32);
   - the state's media manifest.
4. **Space:** diffusion and reverb.
5. **Perform UI:** the single-lane canvas, the 8 macros, Texture and Feedback.
6. **Close:** listening gate #1, the CPU baseline and the §16 headline judged again, and the
   host matrix.

**Director decisions** (2026-10-27, during brainstorming):

- **A thin vertical slice first**, not risk-first or two parallel tracks. It puts sound in front of
  ears early, with final interfaces; the renderer's internals get swapped once, in sub-project 2.
- **The first sound is the FX on live input.** There is no file I/O and no new UI, so this is the
  shortest path to a texture in Ableton Live and Bitwig. The instrument gets its sound in
  sub-project 3.
- **A shared, JUCE-free core.** The patch, the parameter-to-engine mapping and the FX processing
  live in one library, used by both the plugin and `gg-render`. Goldens then test exactly what a
  DAW plays.

## 2. Goals

- Giga Grains FX on any track in Ableton Live 12 or Bitwig Studio turns the track's audio into a
  Cloud texture.
- Ten parameters (§4) are automatable and modulatable, and are saved and restored with the
  session.
- **The carry-over's first item is done.** The plugin audio path (`process`, `params.flush`, the
  dry/wet sum) builds and runs under ASan and RTSan, through a JUCE-free plugin core.
- **Everything stays deterministic.**
  - The FX output, input included, is bit-identical at any host block size.
  - The grain-event log, now with pan and gain, still matches exactly across macOS, Windows and
    Linux.
- **clap-validator runs in full:** the `param-conversions` exclusion (ruling T8-c) goes.

## 3. Non-goals for this slice

All of these come in later sub-projects:

- SIMD kernels, the SoA pool and the CPU gap (sub-project 2). The slice keeps M0's scalar Hermite
  renderer.
- Sample loading, the instrument's sound, note-triggered freeze/capture and the media manifest
  (sub-project 3).
- Diffusion and reverb (sub-project 4).
- The canvas, macros, envelope shape (Texture) and Feedback (sub-project 5). The editor keeps
  M0's stress panel; hosts show their generic parameter views.
- Minutes-long live buffers with persistence, the budget manager, mipmaps and the HQ tier (M2).

## 4. Parameters

**ID scheme:** `id = (scope << 16) | index`.

- Scope 0 is global. Scopes 1 to 4 are layers A to D. Only layer A exists in M1, so M2 adds
  layers without renumbering. Index 0 is reserved.
- Every ID is below `0xB00000` (ruling T8-d), and IDs are append-only.
- The IDs are pinned by a test. INTENT §18 freezes them at the first public release.

| ID | Name | Scope | Range | Taper | Default | Applied |
|---|---|---|---|---|---|---|
| `0x000001` | Mix | global | 0–100% | linear | 50% | smoothed per sample |
| `0x000002` | Level | global | −60 … +12 dB | dB | 0 dB | smoothed per sample |
| `0x010001` | Position | layer A | 0–16 s behind the write head | log-skewed | 0.25 s | latched at each grain's onset |
| `0x010002` | Size | layer A | 5 ms – 2 s | log | 150 ms | latched at onset |
| `0x010003` | Density | layer A | 0.25–400 grains/s | log | 16 | scheduler, on the control grid |
| `0x010004` | Distribution | layer A | 0–100% (regular ↔ random) | linear | 50% | scheduler, on the control grid |
| `0x010005` | Pitch | layer A | −24 … +24 semitones | linear | 0 | latched at onset |
| `0x010006` | Spray | layer A | 0–2 s random read-position spread | log-skewed | 50 ms | latched at onset |
| `0x010007` | Stereo | layer A | 0–100% per-grain pan spread | linear | 50% | latched at onset |
| `0x010008` | Freeze | layer A | off/on (stepped) | — | off | write-gain crossfade (§6) |

**Representation.** Parameters are sst-basic-blocks `ParamMetaData` entries in an sst-plugininfra
`PatchBase` patch.

- The CLAP value is whatever `ParamMetaData` represents for the taper; for log ranges this may be a
  log-domain value.
- Display text, units and text-to-value parsing come from the metadata.
- A test pins every parameter's ID, name, range, default and a value ↔ text round trip.

**CLAP flags:**

- Every parameter is `AUTOMATABLE` and `MODULATABLE`. A modulation event adds an offset to the base
  value: it is clamped to the range and never written back to the base. That is the "base +
  modulation" half of INTENT §14's parameter model; the per-grain spread half is Spray, Stereo
  and Distribution.
- Freeze is also `STEPPED`.
- In the **instrument**, Mix is `CLAP_PARAM_IS_HIDDEN` and has no effect, because the instrument
  has no dry signal. The parameter list stays identical in both products, so they share one state
  format (INTENT §2).

**Remote controls:** one 8-slot page, "Cloud": Position, Size, Density, Pitch, Spray, Stereo,
Freeze, Mix. The 8 macros proper arrive in sub-project 5.

**Seed:** the engine seed is saved in the state. New instances start at seed 0, so the slice is
fully deterministic. Whether fresh instances should draw distinct seeds is decided in
sub-project 5.

## 5. Architecture

```
engine/   framework-free (unchanged rule; INTENT §19)
          + CloudScheduler   replaces PeriodicStream
          + LiveSource       a ring buffer; Sample and Live sources behind one read interface
          + logDet           a deterministic natural log (MathDet), like exp2Det
core/     NEW gg-core (JUCE-free)
          + Patch            PatchBase + ParamMetaData; patch version 1; migration hooks
          + ParamMap         patch values → engine parameters (the only mapping)
          + Processor        owns the Engine; applies parameter events on the control grid;
                             smooths Mix and Level; writes FX input into the live ring;
                             runs the wet chain and the dry/wet mix; real-time safe
plugin/   a thin CLAP adapter: params, state and remote-controls → Patch/Processor
          createEditor() in its own translation unit; editor glue only under GG_BUILD_EDITOR
tools/gg-render   FX mode: an input signal through the same Processor
```

- **Dependencies.**
  - Adds the sst-plugininfra submodule (MIT), already listed in INTENT §15. It's pinned by SHA,
    with the tag or branch recorded.
  - Only its `patchbase` interface target and the bundled tinyxml (zlib) are used.
  - **sst-plugininfra also bundles `libs/asiosdk`, which must never be built.** It's the same
    problem as M0's ruling F4: the files carry no license grant. The `patchbase` target doesn't
    need it. A CMake guard, or a review check, keeps it out.
- **`GG_BUILD_EDITOR`.** A new option that defaults to ON. When it's OFF:
  - the plugin core builds without JUCE or `clap_juce_shim`, and doesn't expose CLAP's gui
    extension;
  - the clap-wrapper format targets aren't built;
  - the `[plugin]` tests run in-process through the CLAP factory, as in M0.

  The `asan` and `rtsan` presets set `GG_BUILD_PLUGINS=ON` and `GG_BUILD_EDITOR=OFF`.
- **Threading** (INTENT §15):
  - **Values the main thread reads** (`params.get_value`, value-to-text, state save): an atomic
    per-parameter value store. Only the audio thread writes it while the plugin is active.
  - **Parameter changes from the host** arrive as CLAP events in `process` or `params.flush`.
  - **State load while active:** the new values reach the audio thread through a lossless SPSC
    queue and apply at the start of the next block.
  - **The Processor** never allocates, locks or does I/O. It's annotated
    `SST_CPPUTILS_NONBLOCKING`.
- **Carry-over items closed by the split:**
  - [161]/[292]: the duplicate-library link warning;
  - [210]: `createEditor()` moves to its own translation unit;
  - [211]: the GUI test asserts only each platform's native window API;
  - [212]/[213]/[344]: the JUCE definitions are consolidated beside `add_clap_juce_shim`, with
    version printing turned off;
  - [236]: the clap-helpers checking level is decided with the new sanitizer coverage.

## 6. DSP

### 6.1 Control grid and smoothing

- **The control grid is 32 samples, aligned to absolute sample positions** (INTENT §14: aligned
  to the stream's absolute sample count, not to block starts).
- A parameter or modulation event takes effect at the first grid boundary at or after its
  timestamp, so the result doesn't depend on how the host splits blocks.
- `params.flush` outside `process` applies values immediately.
- **Mix and Level** ramp linearly to each new target over 10 ms, sample by sample. The ramp
  restarts from the current value at every grid change.

### 6.2 CloudScheduler

- **Onsets are a recurrence:** `t(k+1) = t(k) + Δ(k)`, with `Δ(k) = P × ((1 − d) + d × E(k))`.
  - `P = sampleRate / Density`.
  - `d` is Distribution, as a fraction from 0 to 1.
  - `E(k) = −logDet(1 − u(k))`, where `u(k)` comes from the counter RNG keyed by
    (seed, layer, stream, `Field::Onset`, k).
  - The mean interval is always `P`. At `d` = 0 grains are exactly periodic; at `d` = 1 they're
    Poisson; values in between blend the two.
- **Onsets keep fractional sub-sample timing,** as in M0.
- **`Δ(k)` uses the Density and Distribution in effect at `t(k)`'s grid cell.** A change takes
  effect at the next onset computed after its grid point, which supersedes ruling T3-a's block-start
  re-anchor and removes `restartLattice_` (carry-over [104] and A-m1). Density at 0.25/s is the
  floor, so the stream never stops.
- **Every grain field is drawn at spawn,** from the parameters in effect at its onset's grid cell:
  - **delay** = Position + Spray × u, with u in [0, 1). The delay is in seconds behind the write
    head, and §6.3's clamp then applies.
  - **pan** = Stereo × bipolar(u).
  - **rate** = `exp2Det(Pitch / 12)`.
  - **duration** = Size, with a minimum of 16 samples, as in M0.
  - **gain:** see §6.4.

  Each draw has its own `rng::Field`, keyed by (seed, layer, stream, field, k), with the real
  layer and stream IDs (carry-over A-m2). Layer A is layer 0 and its single stream is stream 0.
- **No libm values reach the event log.** `logDet` is a polynomial with argument reduction, with
  `|logDet(x) − log(x)| ≤ 1e-14 × max(1, |log x|)` across (0, 1], down to the smallest
  `1 − u` (2^-53). Its domain excludes 0: `u` is drawn from [0, 1), so `1 − u` is in (0, 1].

### 6.3 LiveSource

- **The ring** is 16 s × the sample rate, stereo float, allocated in `activate()`. A mono input
  fills both channels.
- **Each block writes its input before any grain reads.**
  - Read positions are absolute input-sample indices, never block offsets.
  - Reads come from data already written, so the output is bit-identical at any block size.
- **Admission clamp.** At spawn, the grain's start delay is raised to the minimum that keeps
  every sample it reads, over its whole life, strictly older than the output sample being
  rendered. This holds even for rate > 1, where a grain reads faster than the ring fills.
  - It is also limited so the grain never reads data the ring has overwritten: the start delay
    is at most the ring length minus the grain's read span.
  - When both limits can't hold (a span longer than the ring), the grain is dropped and counted
    in `droppedGrains()`.
- **Freeze.**
  - **On:** the write gain ramps to 0 over 20 ms, then the write head stops advancing. Grains
    keep reading relative to the frozen head, and the wet output continues.
  - **Off:** the head resumes, and the write gain ramps back to 1 over 20 ms.
  - **Head-crossing crossfade:** frozen, the ring's newest and oldest samples sit next to each
    other. Reads across that seam crossfade over 256 samples, INTENT §14's default policy.

### 6.4 Rendering and loudness

- **The renderer** is M0's scalar 4-point Hermite path, extended for:
  - stereo reads from the ring;
  - per-grain equal-power pan, with gains from a deterministic polynomial sin/cos, no libm;
  - per-grain gain.

  The Hann window stays the only envelope.
- **Per-grain gain** is `1 / sqrt(max(1, Density × Size))`, with Density in grains/s and Size in
  seconds. That is the expected overlap; uncorrelated grains add in power. The value is latched
  at spawn, so a moving Density can't pump the level.
- **The wet chain** (in the Processor):

  ```
  Σ grains → × Level → soft limiter → Mix with dry
  ```

  - **Soft limiter:** linear up to |x| = 0.5 (−6 dBFS), then a smooth deterministic polynomial
    or rational saturation to ±1, with no libm. It's a first, minimal form of INTENT §14's limiter.
  - **Mix:** `out = dry × (1 − m) + wet × m`, linear.
  - **The instrument** outputs the wet signal only (Mix is hidden; §4).
- **Denormals:** one `ScopedDenormalsOff` covers the whole `Processor::process`, including the
  dry/wet sum (carry-over [281]/[298]/C-m2). `Engine::process` documents that it restores the
  caller's floating-point mode. The FX's dry pass-through is bit-exact for normal floats.
- **The grain-event log** gains `pan` and `gain`. The cross-platform goldens then pin stereo
  placement and level, which closes part of carry-over B-m7.

## 7. gg-render FX mode

The render spec gains:

- `"mode": "fx"`;
- `"input"`: a built-in `sine`, `noise` or `sweep` signal (mono or stereo), or a WAV file;
- `"params"`: initial values by parameter name;
- `"automation"`: a list of `{time, param, value}` events in seconds.

The input runs through the same `Processor` the plugin uses.

- **Existing M0 cases** stay as engine-level "sample" cases.
  - The engine's `PeriodicStream` is replaced by `CloudScheduler` at Distribution 0.
  - Its onsets come from the recurrence, not M0's `k · sr / rate`, so the event logs change in
    their last bits.
  - Those goldens are regenerated once, and the commit message says why (CLAUDE.md).
- **New golden cases.** Every input is built in, so there are no binary fixtures:
  - an FX texture on a mono sine;
  - an FX texture on a **stereo** input (carry-over B-m8);
  - an FX case that automates Density and toggles Freeze.

## 8. Verification

TDD throughout: each test fails before its code exists.

| Area | Tests |
|---|---|
| Patch | IDs, names, ranges, defaults and value ↔ text pinned; the XML round-trips exactly; the migration hook runs |
| `logDet` | `|logDet(x) − log(x)| ≤ 1e-14 × max(1, |log x|)` across (0, 1], including x = 1 and x = 2^-53 |
| CloudScheduler | at d = 0, intervals are exactly `P`; at d = 1, over 10⁵ grains the mean is `P` within 1% and the coefficient of variation is 1 ± 0.05; at d = 0.5 the mean is `P` within 1%; parameter changes take effect at grid boundaries |
| LiveSource | no grain ever reads a sample at or after its output time, swept across Pitch, Size and Position; no read of overwritten data; freeze and unfreeze stay below the click threshold; the head-crossing crossfade |
| Determinism | the Processor with an input signal, parameter automation and freeze toggles is bit-identical at block sizes 1, 7, 64, 511 and 4096, plus random splits |
| Goldens | the regenerated sample cases, plus the three FX cases in §7 |
| Plugin | parameter info, value and text; `flush` applies changes; modulation offsets, without writing back to the base; state save → load → identical render; the RT tripwire on `process` and `flush` *with* parameter and modulation events; instrument Mix hidden |
| Sanitizers | ASan and RTSan run the engine, `gg-core` and the plugin core (`GG_BUILD_EDITOR=OFF`) |
| Validators | clap-validator in full (exclusion removed from `ci.yml` and CLAUDE.md); pluginval at strictness 10; auval ×3; Steinberg's VST3 validator |

**The CPU cost is unchanged.** The slice doesn't touch the kernels, and `gg-bench` keeps
reporting.

## 9. When the slice is done

1. CI is green on every leg.
2. **Director host check.** Giga Grains FX on an audio track in Ableton Live 12 and in Bitwig
   Studio:
   - turns the track into textures;
   - its 10 parameters show up and automate;
   - Freeze works;
   - a saved project reopens sounding the same.
3. **Director listening check:** a short A/B set of FX renders. This is not listening gate #1,
   which closes M1 in sub-project 6.
4. **INTENT is updated, each change with a dated §23 row:**
   - §14: the Cloud scheduler formula, the 32-sample control grid, the latched gain
     normalization and the soft limiter.
   - §15: the `core/` layer.
   - §18: the parameter ID scheme and the first ten IDs.
   - §20: the M1 sub-project sequence.
   - §23: the three director decisions in §1.
   - `docs/m1-carry-over.md`: the items this slice closes are marked done.

## 10. Risks and open points

| Risk | Handling |
|---|---|
| `ParamMetaData`'s log representation makes CLAP values awkward for hosts (displayed ranges, automation lanes) | The value ↔ text tests plus the director's host check. If it's awkward, switch to natural units with an explicit taper; the IDs don't change |
| Regenerating the M0 event-log goldens hides a real regression | Regenerate only in a commit that changes nothing else in the scheduler, and confirm the old and new logs differ only in onset fractions' last bits (`gg-render --compare` metrics unchanged within tolerance) |
| An admission clamp that is too conservative makes high Pitch audibly late | Measure the delay the clamp adds at +24 semitones, and record it in the listening-check notes |
| sst-plugininfra pulls in more than `patchbase` | Link only `patchbase` and `tinyxml`; a review checks that `libs/asiosdk` is never compiled |
