# M2 sub-project 1: Layers and structure — design

| | |
|---|---|
| **Status** | **Spec written 2026-10-29; awaiting the programme manager's approval and the director's answers to §8.** Not yet planned or built |
| **Milestone** | M2 Giga (INTENT §20, §23 #64), sub-project 1 of 7; wave 1, alone |
| **Spec authority** | `INTENT.md`, then `docs/superpowers/specs/2026-10-29-m2-programme-design.md` (its §6.1, §7 and §12 are this sub-project's charter: realized here, not reopened). This document records how this sub-project implements them |
| **Builds on** | M1 as closed (§23 #60): the `Processor` and its wet chain, the Cloud engine, Sources, Space, the Perform UI's macros, Texture and Feedback; the release pipeline (§23 #61) |
| **Feeds** | Every wave-2 sub-project: 3 (Engine scale and quality) takes the mip-level accessor and the per-layer pools; 2 (Sources and persistence) takes the Source-kind parameter, the per-layer rings and the chain's tap point; 4 (Extremes and the FX chain) takes the chain after the grain sum and the source interface; 5 and 6 take the parameter list, the mod matrix and the layer structure |
| **Starts from** | `m2-programme` at `1f8cb33` (the programme PR #14, in CI), and `docs/m2-carry-over.md` with **13 open rows** |

## 1. Why this sub-project

INTENT §5 promises many layers: four per instance, each with its own source and settings, mixed or
morphed. M1 built one. Everything else M2 owes writes into the layer structure (persistence saves
per-layer media, the budget divides across layers, presets voice every layer and route, the views
draw them), so the programme design's decision 2 puts this sub-project first and alone: wave 2
branches from its merged result.

It delivers the programme design's inventory items I1 to I7:

- **I1** four layers A to D in the engine and the `Processor`, each owning a source, a scheduler, a
  grain stream and its own pool and ring, summed in a fixed order;
- **I2** mix and XY morph of the layers as a mixer law in the engine (the pad is sub-project 6's);
- **I3** rendering split into one task per active layer, run on the host's CLAP thread pool when it
  offers one and in a loop otherwise, bit-identical either way;
- **I4** the thread-pool survey, prepared here with a machine-checkable stand-in and finished at the
  close;
- **I5** the full mod matrix: every §14 source, user routes, the eight macros re-targeted onto it as
  factory routes with their M1 depths;
- **I6** pitch spread as a per-grain field;
- **I7** C++23 with a toolchain floor raise, as the first task, with its revert path.

With them come the parameter structure wave 2 appends into (§7.1 of the programme design), the
Source-kind parameter with its whole enumeration, the patch version bump and its migration (the
instrument's Macro 7 becomes Feedback), the signal chain of §7.2 as code, the remote-controls pages
for layers B to D, and `gg-render` specs that address layers, routes and the new sources.

**Headless by choice** (the programme design, §6.1): the Perform view keeps showing layer A alone.
Layers B to D are reachable through the host's generic editor, automation, the remote-controls pages
and `gg-render` until sub-project 6.

## 2. Goals

1. **Four layers**, each a complete grain engine: source, scheduler, stream, pool, live ring,
   diffuser, Level, Feedback and the soft limiter, exactly M1's chain per layer, summed in order
   A, B, C, D.
2. **Bit-identity with M1 at rest.** With layers B to D off, the morph pad at rest, Pitch Spread 0
   and the matrix holding only the factory routes, every one of the 22 M1 goldens keeps its WAV
   SHA-256, in both products, through every task.
3. **Bit-identity across threads.** A render is the same bytes with no thread pool and with a pool
   of 1, 2 or 8 workers, at every block size.
4. **One modulation model.** The effective value of every parameter stays
   `clamp(base + mod + Σ routes)` in one clamp, where the routes are the matrix's; the eight macros
   are twelve of its slots.
5. **The structure wave 2 needs:** the ID table with the next free index per scope and the three
   reserved blocks, pinned; the Source-kind enumeration; the per-layer rings and pools; the
   mip-level accessor; the chain with its named tap points; the patch at version 2.
6. **No CPU regression on X:** `engine-sample` shows no `regressed` ratio against M1's baselines,
   and the two new cases are published beside it, report-only.
7. **C++23 everywhere**, or a recorded reason it isn't (§6.1).

## 3. Non-goals and anti-goals

**Non-goals (later sub-projects, or later milestones):**
- The Resample and Spectral Smear source kinds (2 and 4): their values exist in the enumeration and
  fall back (§6.4).
- Per-layer samples in the plugins' state and loader (`MEDI` version 2, sub-project 2). In wave 1
  only layer A loads a sample in the plugins; `gg-render` can give any layer one.
- The ring-length parameter, the memory cap and the resample tap (2); the budget manager, per-layer
  quotas and the pool's 16,384 ceiling (3); the global delay, the limiter beyond M1's soft limiter,
  filters and buses (4); presets (5); every view, the layer strip, the pad and the Mod view (6).
- Per-note (polyphonic) modulation and MPE (M3). The MIDI sources here are monophonic channel
  messages.
- Tempo-synced LFOs, steps and grids (M4). The transport source exposes the host's position; nothing
  here locks to it.
- Chunking a layer's grains into further render tasks (§22).
- Mod routes that target a modulator's own controls, or a macro (§9).
- Per-grain spread for fields other than pitch, unless the director chooses otherwise in §8.

**Anti-goals (a failure even if everything works):**
- A render whose bytes depend on whether the host offered a thread pool, on how many workers it
  has, or on which thread ran a task.
- An allocation, lock, syscall or CPU-adaptive branch on the audio thread, on a pool thread
  included.
- A resting preset that renders differently from M1 by one bit, or an M1 session that loads into
  M2 and sounds different.
- A state the M1 build can't skip past (the patch version bump must leave the M1 reader loading
  layer A's values).
- A new parameter index inside a block reserved for wave 2, or a new scope.
- A click when a layer is switched on or off, or when its source kind changes.

## 4. Constraints

- CLAUDE.md's non-negotiables: the audio-thread rules, RTSan-clean, 0- and 1-sample blocks,
  `params.flush` trivial; randomness only from the counter RNG keyed by (seed, layer, stream,
  field, grain index); no `std::` distributions; no CPU-adaptive behaviour; layers summed in a fixed
  order; block-size invariance; 64-bit positions; the overload ladder's order; licensing.
- INTENT §8.2 (one engine, many faces), §8.3 (uniform source interface), §8.6 (deterministic) and
  §8.9 (machines verify) shape the design; §8 breaks ties.
- The programme design's §7: IDs `(scope << 16) | index`, globals from `0x000019`, layer scopes
  mirroring scope 1, the reservations `0x..40`–`0x..4F`, `0x..50`–`0x..5F` and `0x..60`–`0x..7F`
  (§7.1); the chain and the per-layer ring (§7.2); the matrix's rules, the macros as factory routes
  targeting every layer, Macro 7's switch here (§7.3); tasks are the layers, no plugin-owned thread
  (§7.4); the container at version 1, this sub-project alone bumping the patch version, routes in
  `PTCH` (§7.5); every M1 hash kept, X unchanged in definition (§7.6); C++23's terms (§7.9).
- Ownership (§6.1): `engine/`, the `Processor`'s layer and routing structure and the chain, the
  source interface's read and mip accessor, the Source-kind parameter, the patch migration,
  `ParamIds.h` and `ParamMap.h`, `tools/gg-render`'s spec, the toolchain. Shared files
  (`Processor`, `ParamIds.h`, `tests/CMakeLists.txt`, INTENT) by addition only. Nothing in `ui/`
  changes beyond what compiles.
- The process: spec → plan → subagent-driven build → fresh review → fix wave; `scripts/check.sh`
  over `dev,asan,rtsan`; CI green through a draft PR; `scripts/update-goldens.sh` before and after
  each DSP change with the hash list in the commit; the carry-over counted at start and end.

## 5. Approaches

**Where the layers live.** Three were considered.

1. **`Engine` stays one layer; the `Processor` owns four (chosen).** `gg::Engine` keeps its M1
   contract (one Cloud stream reading one source, X measured through its `process`) and gains the
   layer index for its RNG key, the Source-kind switch, Pitch Spread and an enable gate. The
   `Processor` owns four `Layer`s, each an `Engine` with its own chain state, and is where the tasks,
   the morph sum and the matrix live, since it already owns the control grid, the ramps, the notes
   and Space, and the plugin and `gg-render` drive it. *Cost:* the "engine" of §6.1 is two classes
   (`Engine` per layer, the `Processor` as the instance); the task interface sits in `engine/` so
   `gg-bench` and the tests use it without `gg-core`'s chain.
2. *`Engine` becomes the instance:* four internal layers rendered into per-layer output buffers,
   the chain applied outside. Rejected: the per-layer chain (diffuser, Level, soft limiter, Feedback
   into the layer's ring) sits between a layer's grains and its ring input, so the engine would have
   to call back into `gg-core` per piece, or the chain would move into `engine/`, where the ramps
   and Space don't belong.
3. *A new multi-layer class between them.* Rejected as a third home for state the `Processor`
   already holds (ramps, the grid, the held set).

**The task boundary.** A task is one layer's whole piece: its ring input, its grains, its
diffuser, Level, soft limiter and the feedback history, into its private wet buffer. The
alternative, grains only in the task and the chain serial after the join, halves what runs in
parallel for no gain in simplicity: the chain is per layer anyway, and the ring input of piece
*n* needs the layer's own limited output of piece *n − 1*, which the task has.

**The morph law** is a director's choice (§8, D1); each option is stated with its cost there.

**The matrix's home.** Routes live in the patch (`PTCH`), as §7.5 decided, never as CLAP
parameters; the modulators' own controls are ordinary parameters per layer. The alternative, routes
as parameters, was closed by the programme design (it burns IDs and lets hosts automate routes).

**C++23's Linux leg.** gcc-13 on ubuntu-22.04 from the `ubuntu-toolchain-r/test` PPA (chosen):
INTENT §15's glibc baseline stays. The alternative, ubuntu-24.04 with its native gcc-13, moves the
baseline to glibc 2.39 and is not taken silently; if the PPA route fails in the timebox, the whole
task reverts (§6.1).

## 6. Design

### 6.1 C++23, the first task (I7; programme design §7.9)

- `CMAKE_CXX_STANDARD 23`, `CMAKE_CXX_STANDARD_REQUIRED ON`, extensions off, for every first-party
  target (through `gg::compiler-options` and the top-level `CMakeLists.txt`). Third-party targets
  keep what they set.
- **The toolchain floor:** GCC 13 or newer (the configure fails, naming the floor, below it); Apple
  clang and ClangCL as they are (both compile C++23 today); upstream LLVM 20 for RTSan as before.
  The macOS 11.0 deployment target gates only the libc++ features we then avoid: nothing here uses
  a library feature that libc++ marks unavailable before macOS 11.
- **CI's Linux leg** stays on `ubuntu-22.04` and installs `gcc-13`/`g++-13` from the
  `ubuntu-toolchain-r/test` PPA (`prepare-for-juce` with `gccversion: 13`, with the PPA added
  first if the action doesn't). The preset `linux-gcc12` becomes `linux-gcc13` (compiler, display
  name, build and test presets), and with it `tools/gg-bench/baselines/linux-gcc12.json` is renamed
  and its `leg` field updated, since `--make-baseline <preset>` keys the file by the leg; CLAUDE.md
  and `ci.yml` follow. Historical documents keep the old name.
- **What C++23 is used for here:** `std::expected` for the new fallible main-thread paths (the
  patch migration's parse of the routes, the render spec's new keys), `std::to_underlying` for the
  enumerations, constexpr tables for the ID table. The audio path is unchanged
  in this task: it is a standard bump, and `scripts/update-goldens.sh` before and after must list
  every M1 hash unchanged.
- **The timebox** is one plan task, including two CI rounds. If any leg can't be made green in it,
  the task's commits are reverted (`git revert`, so the branch records the attempt), INTENT §22's
  C++23 row records which leg failed and why, and the rest of this sub-project proceeds on C++20.
  Nothing else here depends on the standard.

### 6.2 Layers in the engine and the `Processor` (I1)

**`gg::Engine` is one layer's engine.** Its M1 contract holds (`Engine.h`'s class comment), with
these additions:

- `EngineConfig::layer` (0 to 3): the `layer` field of every RNG key its scheduler draws. Layer A is
  0, as M1 always keyed it, so its draws are M1's bit for bit. Stream stays 0 (M3's Voice scheduler
  adds streams).
- **A source kind per layer** (§6.4): Sample or Live, switchable on the audio thread; the grains
  sounding at a switch finish on the source they started on.
- **Pitch Spread** (§6.6) in `CloudParams`.
- **An enable gate** (`setEnabled`): off, the scheduler emits nothing, as the note gate holds it;
  on again, the pending onset is forced to that boundary, as a note-on forces it (§9). The ring is
  written and the clock advances whether or not the layer is enabled, so a layer switched on after
  a minute reads recent audio.
- **The mip-level accessor** (§6.5).

**Each layer owns its pool,** of `EngineConfig::grainCapacity` grains (4,096 by default, as M1's
one pool), its scheduler, its ring and its two sample slots. So an instance holds four pools,
16,384 grains in all; the per-instance budget and quotas are sub-project 3's. A pool, a ring and
the per-layer buffers are allocated for every layer at construction, whatever its kind and whether
it is on: a kind switch or an on/off is a parameter change on the audio thread, which may not
allocate.

**The `Processor` is the instance.** It owns `kLayers = 4` `Layer`s, each: an `Engine`
(`layer = its index`), a `Diffuser`, ramps for Level, Diffusion, Feedback and the layer's weight
(§6.3), the wet and ring-input buffers, the 256-sample limited history for Feedback, a
`SourceExchange` for its sample, a `LivePeaks` strip, and a fixed grain-event buffer (§6.7). Its
global state is M1's: Mix, the reverb and its ramps, the held set, the grid, publishing, plus the
morph (§6.3) and the matrix (§6.8).

`setSourceNow` and `sources()` take a layer (the M1 signatures stay as layer A's); the plugin's
loader hands samples to layer A alone until sub-project 2's `MEDI` version 2, and `gg-render` to
any layer (§6.11).

**Both products have four rings.** Each layer's ring is `kLiveSeconds` = 16 s (the ring-length
parameter is sub-project 2's), fed from the instance's input: the FX's input, and the instrument's
if D2 (§8) gives it one, else silence. A layer's ring is written on every piece while the layer is
on, whatever its kind, as M1's engine always recorded in either mode; off, it is written too (the
enable gate above), so nothing about the ring depends on the layer's history except its frozen
state. Memory per instance at 48 kHz: four rings about 25 MB, four pools about 5 MB, the reverb
14.5 MB, the event buffers about 2.4 MB; the memory cap is sub-project 2's business (programme
design §7.2).

**Pieces.** Both products render in pieces of at most `kFeedbackDelay` = 256 samples (M1's FX did;
its instrument rendered whole blocks). Piece size changes no bit: block-size invariance guarantees
it, and the hash list proves it in the task that makes the change. Each piece runs the active
layers' tasks (§6.7), then the serial tail: the morph sum, the reverb, the mix.

### 6.3 The chain (programme design §7.2) and the morph (I2)

Per piece, with the programme design's diagram made exact:

```
per layer L (A..D), as one task:
   ring_L input[t] = sanitize(in[t]) + fb_L(t) · limited_L[t − 256]      (Live kind writes it; §6.4)
   w_L   = Σ grains_L                                                   (the engine, spawn order)
   w'_L  = δ_L > 0 ? w_L + δ_L·(diffuse_L(w_L) − w_L) : w_L
   limited_L = softLimit(w'_L · g_L)                                     (Level_L's gain; Feedback_L reads here)
   wet_L = limited_L · weight_L(t)                                       (the morph weight × the on/off ramp)

instance, after the tasks join:
   wet = wet_A ⊕ wet_B ⊕ wet_C ⊕ wet_D      (⊕: the first on layer assigns, each later on layer adds; off layers are skipped)
   FX:          rIn = m·wet + s·d    out = d·(1 − m) + wet·m   [+ softLimit(a·r)]
   instrument:  rIn = wet            out = wet                 [+ softLimit(a·r)]
```

- **M1's chain is kept per layer,** Level and Feedback included, so one layer on at morph rest
  renders bit-identically to M1: `weight_A` is exactly 1.0 there (below), `limited_A × 1.0` is
  exact, and the sum *assigns* the first on layer rather than adding it to zero, so even a −0 would
  survive. The global delay and limiter (sub-project 4) rest as the identity when they arrive;
  the resample tap (2) reads after the mix.
- **Level per layer.** Layer A's Level is M1's global `Level` (`0x000002`); layers B to D get
  `Level B`, `C`, `D` in scope 0 (§6.9), as the programme design placed them. Each ramps its
  linear gain over 10 ms like M1's.
- **On/off per layer** (`Layer A On` … `Layer D On`, stepped; A on by default, the others off).
  Turning off ramps the layer's weight to 0 over 10 ms; when the ramp reaches 0 the layer is *off*:
  its engine is disabled (no spawn; the pool is cleared, so nothing pops back in mid-grain later),
  nothing of it is rendered or added, and its task isn't dispatched (its ring write runs inline).
  Turning on enables the engine at the boundary (the forced onset, so it speaks at once) and ramps
  the weight back. A layer that is off costs its ring write and nothing else.
- **The morph** is two globals, `Morph X` and `Morph Y` (0 to 1, default 0.5 each), ramped like Mix.
  With `x`, `y` their ramped values per sample and `r_L` each layer's on/off ramp, the bilinear
  corner weights are `b_A = (1 − x)(1 − y)`, `b_B = x(1 − y)`, `b_C = (1 − x)y`, `b_D = xy` (A top
  left, B top right, C bottom left, D bottom right, the pad's corners in 6), and the layer weight
  follows the director's law (D1):
  - **D1 option 1 (recommended):** `weight_L = r_L·b_L / Σ_K r_K·b_K` over the on layers, the
    sum-normalized vector law: at the centre every on layer weighs `1/n`, at a corner that layer
    alone, and the amplitude sum is 1 wherever the pad is. One layer on gives `0.25/0.25 = 1.0`
    exactly, whatever the pad does.
  - *Option 2:* `weight_L = r_L·b_L / max_K b_K`: the nearest layer stays at full level, the others
    fade with distance; four layers at the centre weigh 1 each.
  - *Option 3:* `weight_L = r_L·b_L` with the pad resting at corner A (0, 0): plain bilinear.
- Inside the law `x` and `y` are clamped to [0.001, 0.999], so every corner weight is positive
  and option 1's denominator vanishes only when every on layer's ramp is 0, where every weight is 0
  by definition (`s > 0 ? r_L·b_L / s : 0`). So one layer on always gives `b_A / b_A`, exactly
  1.0, wherever the pad is.
- Every weight is plain float arithmetic per sample, no libm; a −0 weight can't arise since the
  ramps and bilinears are products of non-negative values.

### 6.4 The Source-kind parameter (programme design §7.2)

- `Source`, a per-layer stepped parameter (`0x..0C`), whose enumeration is complete from wave 1:
  **0 Sample, 1 Live, 2 Resample, 3 Spectral, 4 Oscillator.** Its default differs by product, as
  Position's does: Live in the FX, Sample in the instrument. The range is fixed now so no frozen
  parameter ever changes range.
- **Built kinds in wave 1: Sample and Live,** in both products. An FX layer set to Sample reads the
  sample `gg-render` gives it, or nothing (silence) in the plugin until sub-project 2's `MEDI` v2;
  an instrument layer set to Live reads its ring, fed from the instrument's input (D2) or silence.
- **Unbuilt kinds fall back to the product's default kind** (Resample, Spectral and Oscillator
  render as Live in the FX and Sample in the instrument), the parameter keeping its value, so a
  state saved with Resample selected plays as Resample the day sub-project 2 lands. The fallback is
  one function, `builtKind(kind, mode)`, that 2 and 4 edit when their kinds arrive.
- **Position and Spray follow the layer's kind,** not the product: Live reads seconds behind the
  head, one-sided (ruling M1-k); Sample reads the fraction of the sample with Spray bipolar in
  seconds (sub-project 3's rule). That is what the engine's two modes already do; the plugin asks
  the host to rescan the parameter *text* (`CLAP_PARAM_RESCAN_TEXT`, main thread) when a layer's
  kind changes, so Position shows seconds or percent accordingly. Ranges are unchanged.
- **Feedback follows the kind too:** it writes a Live layer's ring, in either product, and does
  nothing on a Sample layer (no writable buffer) until the resample source. So Feedback is no
  longer hidden in the instrument, and the instrument's Macro 7 (§6.10) acts on its Live layers.
  Mix and Dry to Reverb stay hidden in the instrument, which has no dry path.
- **A kind switch doesn't click:** at its boundary the scheduler draws for the new kind; the grains
  still sounding finish on the source they started on, as a sample swap already lets them (each
  grain's pool row records which of the two sample slots or the ring it reads, and the renderer
  dispatches per grain in spawn order, so the summation order is unchanged). The pool is no longer
  cleared on a switch. This closes carry-over rows 118 and 119 (§12), which the switch's reach in
  both products makes this sub-project's.

### 6.5 The source interface and the mip-level accessor (programme design §6.1 "Owns")

- The engine's sources (`PreparedSource` over a `SampleBuffer`, and `LiveSource`) gain a
  **level accessor:** `levelCount()`, 1 in wave 1, and `level(n)`, the view of level `n`, which for
  `n` ≥ `levelCount()` returns level 0. Each level view is what the kernels read today (the planar
  channels and the interleaved pairs for a sample; the ring view for the live ring) with its frame
  count and its decimation factor (1 for level 0). The kernels take their taps through the view of
  level 0, so nothing moves; sub-project 3 fills the pyramid and chooses the level per grain
  behind the same accessor, and 2 builds the ring's levels incrementally.
- The read contract is otherwise M1's (`read(position, rate)`, batched by the kernels).

### 6.6 Pitch spread (I6)

- `Pitch Spread`, per layer (`0x..0D`), in semitones, default 0. Each grain's pitch becomes
  `clamp(Pitch + (key − 60) + spread × bipolar(u), −48, 48)`, with `u` keyed by (seed, layer,
  stream, `Field::PitchSpread` = 5, k), a new field appended to `rng::Field`, so no existing draw
  changes. At spread 0 the added term is ±0 and the pitch is bit for bit M1's; the renderer may
  skip the hash then, since a stateless draw has no side effect.
- The range and shape are the director's (D4): 0–24 st uniform is recommended.
- The grain-event line's `rate` carries it, so the event-log gate sees it on every platform.
- The canvas's y axis stays pan until sub-project 6 frees it for pitch (INTENT §17).

### 6.7 Threads: the tasks and the pool (I3, I4; programme design §7.4)

- **`gg/engine/TaskRunner.h`** (framework-free): `run(count, fn, ctx)` runs `fn(ctx, i)` for every
  `i` below `count` and returns when all have finished. `SerialRunner` loops on the calling thread.
  The plugin's runner wraps `clap_host_thread_pool`: `request_exec(host, count)` when the host
  offers the extension and accepts the request, and the serial loop when it declines or doesn't
  offer it (CLAP's own fallback rule). The host calls back into `clap_plugin_thread_pool.exec(plugin,
  task_index)`, which the plugin forwards to the `Processor`'s current task list. VST3 and AU hosts,
  through clap-wrapper, never offer the pool, so they loop.
- **The tasks are the on layers,** one each, per piece (§6.2); a piece with one on layer runs it
  inline without a request. Each task is self-contained: it touches only its layer's state and its
  own buffers. Task `i` of a piece is always the same layer's work whatever thread runs it.
- **Determinism across threads:** every task sets `ScopedDenormalsOff` on its own thread for its
  duration, since the audio thread's floating-point mode isn't the pool thread's; every value a
  task reads of shared state (the piece's bounds, the ramps' coefficients, the parameters in
  effect) is fixed before the request and read-only during it; nothing in a task calls the host,
  allocates, locks or touches the RNG with state. The sum after the join is in layer order.
- **Grain events.** The engine's sink is called from the task's thread, so each layer records its
  piece's spawns into a fixed per-layer buffer of 2 × capacity `GrainSpec`s (the pool's storage
  bound on spawns per block) during the task; after the join the `Processor` drains the buffers in
  layer order A to D and calls the external sink (`gg-render`'s log) and the UI grain queue
  (single producer, as before) in that order. The event log is therefore ordered by piece, then
  layer, then spawn; for one layer it is M1's order.
- **The grain-event line gains `"layer"`** after `"i"` (`{"i":…,"layer":0,"onset":…}`), which
  rewrites every expected `.events.jsonl` once with every WAV unchanged, as `"tex"` did
  (§23 #58). `GrainVis` gains `layer` too, for 6's lanes.
- **The thread-pool invariance test** (programme design §7.4): a stub pool in the tests
  (`std::thread` workers and a barrier, off the audio path) with 1, 2 and 8 workers, and
  `SerialRunner`, render the four-layer specs at block sizes 1, 32, 64, 511 and 4096; every
  render is byte-identical, and the RTSan build runs the tasks on the stub's threads too.
- **The stand-in for the host survey (I4):** the headless plugin harness supplies a fake
  `clap_host_thread_pool` whose `request_exec` runs the tasks on its own threads, and the `[plugin]`
  tests check that the plugin asks for exactly the on-layer count and renders the same bytes with
  the fake pool, with a declining pool and with none. Which DAWs besides Bitwig accept the request
  under load is the close's checklist item (§22), as the programme design says.

### 6.8 The mod matrix (I5; programme design §7.3)

**Slots.** 48, each `{source, sourceLayer, target, targetLayer, depth, curve}`, stored in the patch
(`PTCH`, §6.10), never as parameters. The INIT patch fills slots 1 to 12 with the factory routes
(below); a user may edit or clear any slot. Evaluation is in slot order.

**Sources,** each a value in [0, 1] on the 32-sample control grid, deterministic:

| Source | Scope | Value | Determinism |
|---|---|---|---|
| Macro 1–8 | global | the macro's own effective value `clamp(base + mod)` | as M1 |
| LFO 1, LFO 2 | per layer | shape at the phase; shapes Sine, Triangle, Saw Up, Saw Down, Square, each 0–1 centred on 0.5 (a sine via a deterministic polynomial, no libm) | the phase accumulates `rate × 32 / sr` per grid cell from 0 at activation or reset, so a rate change doesn't jump it; a function of the clock and the parameter history |
| Envelope | per layer | an ADSR (Attack, Decay, Sustain, Release) gated by the instance's held set: on while any key is held | stepped per grid cell with plain float arithmetic; the gate changes at boundaries |
| Random | per layer | a new uniform value every `1/rate` s (a step counter advanced per cell), slewed toward it by Smooth | `uniform01(hash(seed, layer, 0, Field::ModRandom = 6, step))` |
| Step | per layer | Step 1–8's values at `rate` steps per second, Length of them, cycling | the step counter from the clock |
| Mod Wheel, Aftertouch, Pitch Bend | global | the last MIDI CC 1, channel pressure and pitch bend (centre 0.5) on the note port's MIDI dialect | events on the grid like notes |
| Velocity, Key | global | the last note-on's velocity and key / 127 | ditto (CLAP and MIDI note-ons) |
| Env Follower | global | a peak follower of the instance input, fixed 5 ms attack and 200 ms release, clamped to 1 | updated per cell from the previous cell's input, before the tasks run |
| Transport Beat, Transport Bar, Transport Playing | global | the host's beat phase, bar phase and playing flag from its transport event, held at the last value when none is given, 0 before any | the host's events alone; `gg-render` supplies a fixed transport or none |

The per-layer sources' controls are parameters in the layer's scope (§6.9); the global sources have
none. A per-layer source's `sourceLayer` is A to D or *Same* (the target's layer), the default.

**Targets:** any parameter but the eight macros and the modulators' own controls (§9), on one
layer or on **every layer** (the programme design's target model, decision 7). "Every layer" for
Level means the four Levels (A's global and B–D's). Globals without a layer (Mix, the reverb's,
Morph X and Y, the Ons) take no layer.

**Curves,** chosen so the eight M1 recipes are expressible exactly: `Unipolar` u, `Bipolar`
2u − 1, `BipolarPositive` max(2u − 1, 0), `UnipolarSquared` u², `RampThreeQuarters`
min(u / 0.75, 1), `TopQuarter` smoothstep(0.75, 1, u). A route's contribution is
`depth × curve(source)` in the target's CLAP domain, added in slot order; the effective value is
`clamp(base + mod + Σ)` in one clamp. The curves are the same float expressions as
`macroOffsets` today, in the same order, so the factory routes reproduce M1's offsets bit for bit,
moving or at rest (every factory route is exactly +0.0f at rest: the bipolar ones at 0.5, the
unipolar at 0).

**The twelve factory routes,** in slot order, each targeting every layer except Mix:

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
| 12 (FX) | Macro 8 | Mix (global) | +0.5 | Bipolar |
| 12 (instrument) | Macro 8 | Level (every layer) | +12 | Bipolar |

The depths are M1's, kept; revisiting them is the director's at the gate (§23 #58). The
`macroOffsets` function becomes the matrix's evaluation of these slots, and a test keeps them equal
to the M1 function's values bit for bit over random macro vectors until it is removed.

**Timing.** Sources are computed at each grid cell's first sample; routes are summed per target;
the result reaches the engine as `setCloudParams` at the boundary, as macros do today, and a target
that ramps (Mix, Level, Diffusion, Feedback, the morph, the weights) ramps from the boundary. A
fast LFO on a ramped target therefore lags by the 10 ms ramp, M1's semantics. A `setBase` of a
macro, a state load and `reset()` jump the ramps as ruling PU-T2a says; Macro 6 is still seen as
Freeze for arming, per layer.

### 6.9 Parameters: the ID table (programme design §7.1)

The scheme is unchanged: `id = (scope << 16) | index`, scope 0 global, scopes 1 to 4 layers A to
D; every ID below `0xB00000`; append-only. **Layer parameters keep scope 1's indices in scopes 2 to
4,** so `slotForId` is arithmetic, and the slot space is flat: the 28 M1 slots first (their order
kept), then layer A's new parameters, then layers B, C and D in full, then the new globals, 158 in
all (`kParamCount`). Every (name, ID) is pinned by `tests/core/patch_tests.cpp`, as M1's were.

**Layer scope (index, in scopes 1–4):** `01` Position, `02` Size, `03` Density, `04` Distribution,
`05` Pitch, `06` Spray, `07` Stereo, `08` Freeze, `09` Texture, `0A` Feedback, `0B` Diffusion (M1),
then this sub-project's, each `AUTOMATABLE | MODULATABLE`, stepped ones also `STEPPED`:

| Index | Name | Range | Default |
|---|---|---|---|
| `0C` | Source | stepped: Sample, Live, Resample, Spectral, Oscillator | FX Live; instrument Sample |
| `0D` | Pitch Spread | 0–24 st (D4) | 0 |
| `0E` | LFO 1 Rate | 0.01–50 Hz, log | 1 Hz |
| `0F` | LFO 1 Shape | stepped: Sine, Triangle, Saw Up, Saw Down, Square | Sine |
| `10` | LFO 2 Rate | as LFO 1 | 0.25 Hz |
| `11` | LFO 2 Shape | as LFO 1 | Triangle |
| `12` | Env Attack | 1 ms – 10 s, log | 10 ms |
| `13` | Env Decay | 1 ms – 10 s, log | 200 ms |
| `14` | Env Sustain | 0–100% | 100% |
| `15` | Env Release | 1 ms – 10 s, log | 200 ms |
| `16` | Random Rate | 0.01–50 Hz, log | 2 Hz |
| `17` | Random Smooth | 0–100% | 0% |
| `18` | Step Rate | 0.01–50 Hz, log | 4 Hz |
| `19` | Step Length | stepped 1–8 | 8 |
| `1A`–`21` | Step 1 … Step 8 | 0–100% | 0% |

**Next free layer index: `0x22`.** The counts per layer are D3's; the table shows the recommended
option, and another choice shifts only the rows from `0E` on.

**Scope 0 (global):** `01` Mix, `02` Level, `03`–`09` the reverb's and Dry to Reverb, `11`–`18`
Macros 1–8 (M1; `0A`–`10` were never used and stay unused), then:

| ID | Name | Range | Default |
|---|---|---|---|
| `0x000019` | Morph X | 0–100% | 50% |
| `0x00001A` | Morph Y | 0–100% | 50% |
| `0x00001B` | Layer A On | stepped off/on | on |
| `0x00001C` | Layer B On | stepped | off |
| `0x00001D` | Layer C On | stepped | off |
| `0x00001E` | Layer D On | stepped | off |
| `0x00001F` | Level B | −60 … +12 dB | 0 dB |
| `0x000020` | Level C | as Level | 0 dB |
| `0x000021` | Level D | as Level | 0 dB |

**Next free global index: `0x22`.**

**The reservations,** recorded in `ParamIds.h` as named constants and comments, and pinned by a
test that fails if any defined ID's index falls inside them or if the recorded next-free constants
differ from the table's maxima plus one: in scope 0 and in each layer scope, `0x40`–`0x4F` for
sub-project 2, `0x50`–`0x5F` for 3, `0x60`–`0x7F` for 4. Wave 3 adds no parameters.

**Names.** Every layer parameter is displayed with its layer's letter, "A Size" … "D Size", with
the CLAP `module` "Layer A" … "Layer D" for hosts that show paths; globals keep their names.
`gg-render` accepts the displayed names and keeps the bare M1 names ("Size") as layer A's aliases,
so every M1 spec and listening spec is unchanged.

**Metadata that differs between the products** shrinks to Position's default and display, Source's
default and Macro 8's name; Macro 7 is "Macro Feedback", unipolar, 0 by default, in both (§6.10).
The instrument hides Mix and Dry to Reverb; Feedback is shown in both (§6.4).

**Remote controls** (CLAP, 8 slots a page; `page_id` = index; the M1 pages keep 0, 1 and 2):
"Perform" (the macros), "Cloud" (layer A's, as M1), "Space", then **"Layers"** (Layer A–D On,
Level, Level B, C, D), **"Morph"** (Morph X, Morph Y, then six empty slots), and **"Cloud B"**,
**"Cloud C"**, **"Cloud D"** (that layer's Position, Size, Density, Pitch, Spray, Stereo, Freeze,
Level). Eight pages.

### 6.10 The patch at version 2 and the migration (programme design §7.3, §7.5)

- `kPatchVersion` becomes **2**. The container stays version 1; `PTCH`'s XML gains a `<mod>`
  element holding the 48 slots (`<route slot source sourceLayer target targetLayer depth curve/>`,
  empty slots omitted), and the layer parameters' `<p>` entries like any other. This is M2's one
  patch version bump: wave 2 adds parameters absent → default with no bump, and sub-project 2 alone
  adds chunks.
- **Migration from version 1** (an M1 state): every new parameter takes its default (layer A's
  Source takes the product's default kind, so an M1 FX state is Live and an M1 instrument state
  Sample); the twelve factory routes fill slots 1 to 12; and **the instrument's Macro 7 is
  folded:** Stereo's base becomes `clamp(stereo + 0.5f × (2v − 1))` with `v` the saved Macro 7,
  the same float expression the M1 recipe used, and Macro 7 is set to 0, Feedback's rest. The
  render after the migration is bit for bit the M1 render (Feedback 0 on a Sample layer does
  nothing). An FX state's Macro 7 is untouched.
- **A version-2 state in the M1 build** loads through PatchBase's newer-version path: unknown
  `<p>` IDs are ignored and the `<mod>` element unread, so layer A's values load and the factory
  recipes apply as before; this is the "an M2 session an older build can skip past" anti-goal,
  checked here by loading a version-3 state with an extra element into this build (the M1 binary
  isn't available to the tests).
- A pre-container state (bare XML, version 1) migrates the same way.

### 6.11 `gg-render` (programme design §6.1)

The spec format gains, in both `fx` and `instrument` modes, every key as an addition (unknown keys
stay errors):

- parameters and automation by the displayed names, so `"B Size"` addresses layer B and `"Size"`
  stays layer A;
- `"samples"`: a map from layer letter to a source spec (file or built-in), giving that layer a
  sample in either mode; the instrument's `"sample"` is layer A's and may not be given with
  `"samples"."A"`;
- `"mod"`: a list of routes `{"slot", "source", "sourceLayer", "target", "targetLayer", "depth",
  "curve"}` by name, applied before the render on top of the factory routes (a slot named twice is
  an error; `"source": "None"` clears a slot);
- `"controls"`: a time-ordered list of `{"time", "kind", "value", "channel"}` with kinds
  `modWheel`, `aftertouch` and `pitchBend`, as MIDI events on the grid; notes gain an optional
  `"velocity"` (default 100);
- `"transport"`: `{"tempo", "playing", "startBeat"}`, a steady transport the render advances
  itself, or absent (the sources hold at 0).

Sample mode is untouched, so the M0 and Eco goldens don't move. The event line gains `"layer"`
(§6.7).

### 6.12 `gg-bench` (programme design §6.1, §7.6)

Two engine cases, report-only beside X, both in the default case list so CI's legs time them and
the close's `--make-baseline --merge` can add their entries after three full runs:

- **`engine-layers`:** four `gg::Engine`s (layers 0 to 3, each seeded and configured as
  `engine-sample`, each with 500 grains of 100 ms over the shared stereo noise sample) rendered
  through `SerialRunner` and summed in layer order, as a host without a pool pays for a four-layer
  instance at X's grain count; ns per grain-sample over the 2,000 grains, and its ratio to
  `reference`.
- **`engine-layers-full`:** the same with 2,000 grains per layer, 8,000 in all.

`engine-sample` is unchanged (one `Engine`, 2,000 Normal grains): X keeps its definition. A `[bench]`
test checks both new cases admit their grains across four layers and run. The `reference` kernel
and `Kernels.h` are untouched.

## 7. Determinism and real-time safety

- **The RNG key's `layer` is the layer's index**, 0 for A, so every M1 draw is unchanged; the new
  fields `PitchSpread` (5) and `ModRandom` (6) are appended to `rng::Field`.
- **Every modulation source is a function of the sample clock, the parameter history and the host's
  events,** never of time, CPU or thread: LFO phases and step counters advance per grid cell;
  random values are counter-keyed; the follower reads the previous cell's input; MIDI and transport
  are events on the grid. A render repeats bit for bit, at any block size, with or without a pool.
- **Layers are summed in a fixed order** after the tasks join; each layer's grains are summed in
  spawn order inside its task, whatever thread runs it; each task sets its own denormal mode.
- **No allocation, lock, syscall or host call on a pool thread or the audio thread.** The tasks'
  buffers, the event buffers and the per-layer state are allocated at construction. The `[rt]`
  tripwire covers `process` with four layers on, a kind switch both ways, an on/off, every source
  active and routes to every layer, and the fake pool's `exec` path; RTSan runs the same.
- **0- and 1-sample blocks** work as in M1: the tasks split nothing finer than a piece, and a piece
  may be 1 sample. `params.flush` stays trivial.
- **Overload** is unchanged in this sub-project: each layer's pool admits up to its capacity and
  drops beyond it; the ladder is sub-project 3's.
- **The patch migration and the matrix's parse run on the main thread** only; the audio thread
  reads the routes from a fixed table swapped at a `process` call's start, like a loaded state.

## 8. Decisions for the director

Each a forced choice; the recommended option is first, with one line on what each costs. Nothing
engineering-only is here (that is §9).

**D1. The morph law** (§6.3).
1. **Sum-normalized vector morph, pad resting at the centre:** the on layers' weights are their
   bilinear corner weights divided by their sum, so the amplitude sum is always 1 and the pad
   crossfades. *Cost:* switching a second layer on lowers the first by 6 dB at the centre; the
   layer Levels restore the balance.
2. *Nearest-corner full, pad at the centre:* the layer nearest the pad stays at full level and the
   others fade with distance; four layers at the centre are all at full. *Cost:* four dense layers
   sum to four times one, hot until sub-project 4's limiter; the pad can only attenuate.
3. *Plain bilinear, pad resting at corner A:* M1's exact weights with no normalization. *Cost:*
   layers B to D are silent until the pad moves, and the resting pad is in a corner.

**D2. An audio input on the instrument** (§6.2, §6.4). The programme design says both products
expose every source kind; a Live layer in the instrument needs input.
1. **Add a sidechain input port to the instrument in wave 1** (a non-main stereo CLAP port, an
   aux bus in VST3, an input bus on the AU), feeding every layer's ring. *Cost:* the instrument
   shows a sidechain in hosts from now on; auval and pluginval must accept an `aumu` with an input
   bus, and if they don't within the task the port is dropped and option 2 holds until
   sub-project 2 solves it.
2. *Defer the input to sub-project 2* (which owns the sources and the "live-input instrument").
   *Cost:* in wave 1 an instrument layer set to Live hears silence, and 2 does the port.

**D3. The modulators per layer** (§6.8, §6.9).
1. **Two LFOs, one envelope, one random, one 8-step sequencer per layer** (22 parameters per
   layer, 158 in all). *Cost:* 88 modulator parameters across the four layers in the hosts' lists
   and in sub-project 6's Edit view.
2. *One of each, 8 steps* (20 per layer, 150 in all). *Cost:* one LFO per layer; a second comes
   as an appended parameter later.
3. *Two LFOs, two envelopes, two randoms and one 16-step sequencer* (36 per layer, 214 in all).
   *Cost:* the longest parameter list; a denser Edit view.

**D4. Pitch Spread's range and shape** (§6.6).
1. **0–24 semitones, uniform bipolar** around the grain's pitch. *Cost:* clusters wider than two
   octaves need the Pitch macro too.
2. *0–48 semitones, uniform.* *Cost:* most of the knob's travel is beyond musical use.
3. *0–12 semitones, triangular* (the sum of two uniforms, denser near the centre). *Cost:* a
   second draw per grain; no wide clusters.

**D5. Which per-grain spreads M2 has** (§6.6; wave 3 adds no parameters, so a spread not added
in wave 1 or 2 isn't in M2). INTENT §14's model gives every field base + modulation + spread with
a distribution; the inventory names pitch alone.
1. **Pitch Spread alone now;** Position (Spray), pan (Stereo) and onset (Distribution) already
   have theirs; Size and Texture spread, and the distribution selector, go to INTENT §22 as a
   post-M2 question. *Cost:* the Edit view shows spread for four fields, not every one.
2. *Also a Size Spread now* (± octaves, uniform, at `0x..0E`, shifting §6.9's rows). *Cost:* one
   more parameter and golden; the distribution selector still waits.

## 9. My calls

Engineering choices, each with its reason.

- **`Engine` stays one layer; the `Processor` owns four** (§5): X's definition is untouched, M1's
  tests and `gg-render`'s sample mode run unchanged, and the chain that sits inside each task
  already lives in the `Processor`.
- **Each layer has its own pool of 4,096:** a task may not share a mutable pool with another, and
  layer A's admission must be M1's; the instance-wide budget is sub-project 3's.
- **A whole layer piece is the task** (§5, §6.7): more runs in parallel and the task is
  self-contained, including Feedback's history.
- **Grain events are buffered per layer and drained in layer order** (§6.7): the sink and the UI
  queue are single-producer, and the log order must not depend on threads.
- **The event line gains `"layer"`,** rewriting the expected logs once (precedent: `"tex"`): the
  cross-platform gate must tell layers apart in the new goldens.
- **A layer switched on speaks at once** (forced onset at its boundary), as a note-on does, rather
  than waiting up to 4 s at Density's floor; off clears the pool after the 10 ms ramp so no grain
  pops back in later.
- **Off layers keep writing their rings:** a Live layer switched on after a minute should read the
  last 16 s, not stale audio; the cost is a copy per piece.
- **Unbuilt source kinds render as the product's default kind** (§6.4): nothing goes silent by
  surprise, and the saved value takes effect the day the kind lands.
- **Rows 118 and 119 close here, not in sub-project 2** (§6.4, §12): the Source-kind parameter
  makes the switch reachable in both products in wave 1, so the hard cut would ship; the fix,
  grains finishing on their source, is the sample swap's existing rule applied to the ring.
  Sub-project 2's scope shrinks by that item; the programme manager is told in the report.
- **Position, Spray and Feedback follow the layer's kind,** not the product (§6.4): the engine's
  modes already define them that way, and the product was only a proxy for the kind.
- **Notes act on every layer:** the held set is the instance's; the FX's note freeze holds every
  Live layer's ring and the instrument's note gate gates every layer; per-layer or per-channel
  note routing is M3's with the Voice scheduler.
- **48 matrix slots:** twelve factory routes leave 36 for the user; evaluation is 48 multiply-adds
  per target per grid cell at most, negligible.
- **Routes may not target a macro or a modulator's control:** both would need an evaluation order
  across routes; a single pass in slot order stays provably deterministic. Revisit with the Mod
  view if users ask.
- **Six curves, M1's recipe shapes:** so the factory routes are bit for bit `macroOffsets`.
- **The LFO and step counters accumulate per grid cell** rather than reading `t × rate`, so a rate
  change doesn't jump the phase; both are still functions of the clock and the parameter history.
- **The follower's coefficients are fixed** (5 ms, 200 ms), as the programme design says, with no
  parameters.
- **Display names carry the layer letter, with bare names as A's aliases in `gg-render`:** hosts
  that flatten names can tell "B Size" from "A Size", and no M1 spec changes.
- **The remote-controls pages of §6.9:** "Layers" and "Morph" give a controller the strip and the
  pad before sub-project 6 draws them; the M1 pages keep their IDs.
- **The preset `linux-gcc12` is renamed `linux-gcc13`** with its baseline file: a preset named
  for a compiler it doesn't use would mislead every reader of CLAUDE.md.
- **Both products render in 256-sample pieces** (§6.2): one piece loop, and Feedback works on any
  Live layer; bits can't change, by block-size invariance, and the hash list proves it.

## 10. Verification

**Unit tests** (Catch2, labels in brackets):
- `[engine][layers]`: `EngineConfig::layer` keys the draws (layers 0–3 draw differently; layer 0
  draws M1's); the enable gate (no spawn off; the forced onset on; the pool cleared at the ramp's
  end); the kind switch both ways with grains finishing on their source; the level accessor returns
  level 0 for every `n`; Pitch Spread at 0 leaves every rate bit for bit, and at 24 stays within
  [−48, 48].
- `[core][layers]`: four layers summed in order (a permutation of the layers' spawns changes the
  bytes; the fixed order doesn't); B–D off renders bit for bit the one-layer `Processor` (a direct
  comparison, not just the goldens); the weight is exactly 1.0 with one layer on under D1's law; the
  on/off ramp's length and no click (the click score); the per-layer Level and Feedback; Feedback
  on a Sample layer does nothing; both products' pieces.
- `[core][mod]`: each source's value function (LFO shapes at known phases; the envelope's segments;
  Random's keyed values and Smooth; Step's cycle and Length; MIDI and velocity/key events on the
  grid; the follower's attack and release; transport held and advancing); the factory routes equal
  M1's `macroOffsets` bit for bit over 10,000 random macro vectors in both products; the one
  clamp; routes to one layer and to every layer; slot order; a route to a macro or a control is
  refused by the parser.
- `[core][patch]`: every (name, ID) pinned; the next-free constants and the reserved blocks; the
  version-1 → 2 migration of a literal M1 FX state and a literal M1 instrument state (factory
  routes installed; Macro 7 folded, Stereo's new base equal to the M1 effective value bit for bit;
  the render after the migration bit-identical to the M1 render); a version-3 state with an extra
  element loads; a `<mod>` round trip; `migratedFromVersion`.
- `[determinism]`: block-size invariance (1, 32, 64, 511, 4096, random splits) of the four-layer
  FX and instrument renders with every source active and routes to every layer; the thread-pool
  invariance test (§6.7) across `SerialRunner` and the stub pool of 1, 2 and 8 workers.
- `[rt]` and `[rtsan]`: §7's list under the tripwire and RTSan.
- `[plugin]`: 158 parameters with the pinned IDs; eight remote pages; the thread-pool extension
  with the fake host pool (count = on layers; identical bytes with the pool, a declining pool and
  none); the instrument's input port if D2 option 1; `process` and `params.flush` under the
  tripwire with four layers; a version-1 state loaded through the CLAP state extension migrates;
  `CLAP_PARAM_RESCAN_TEXT` requested on a kind change.
- `[render]`: the new keys parse and reject as §6.11 says; bare names are layer A's; the event
  line's `"layer"`.
- `[bench]`: `engine-layers` and `engine-layers-full` build, admit their grains across four layers
  and run `--quick`.
- `[build]`: a `static_assert(__cplusplus >= 202302L)` in the tests; the configure's floor message
  checked by a scratch-project test (`asio_guard_*`'s pattern).

**Goldens** (13 new; every WAV of the 22 M1 cases keeps its SHA-256, stated per commit; each new
case catches all sixteen injected clicks or is added to the table with its reason):

| Case | Mode | What |
|---|---|---|
| `fx-layers` | fx | stereo noise; four Live layers on (B Pitch +7, C Size 40 ms, D Pitch −12); the pad at rest |
| `inst-layers-morph` | instrument | four Sample layers (sweep, sine, noise, a sweep an octave down) and one held note; Morph X/Y automated around the square and back to the centre |
| `inst-pitch-spread` | instrument | one held note over a sine; Pitch Spread stepped 0, 2, 7, 12, 24 at 1 s intervals |
| `fx-layer-switch` | fx | layer B on at 1 s, off at 2.5 s, on at 3 s: the ramps and the forced onset |
| `fx-source-switch` | fx | layer A Live → Sample (a generated file) at 1 s → Live at 2.5 s: the sounding grains finish on their source |
| `fx-mod-lfo` | fx | LFO 1 sine 0.5 Hz → Position; LFO 2 square 2 Hz → Pitch ±12 |
| `inst-mod-env` | instrument | the envelope (500 ms, 1 s, 30%, 1 s) → Size and Level; two notes with a gap |
| `fx-mod-random` | fx | Random 4 Hz → Pitch; a second route with Smooth 50% → Stereo |
| `fx-mod-step` | fx | Step 4 Hz, 8 steps → Pitch ±12: a sequence |
| `inst-mod-midi` | instrument | mod wheel ramp → Size; aftertouch → Density; pitch bend → Pitch; velocity → Level; key → Texture, over three notes |
| `fx-mod-follower` | fx | input bursts; the follower → Density |
| `fx-mod-transport` | fx | transport at 120 BPM playing; beat phase → Position; bar phase → Pitch |
| `fx-macros-layers` | fx | Macro Size and Macro Pitch stepped with layers A and B on: decision 7 heard on two layers |

**Benchmarks:** `engine-layers` and `engine-layers-full` run from `release` with their figures in
the report; `engine-sample` shows no `regressed` ratio against `tools/gg-bench/baselines/` on any
CI model at the sub-project's end (a regression is fixed here, programme design §7.6).

**Validators:** clap-validator, pluginval, auval (all three identities) and Steinberg's validator
on `release`, on every CI leg, with 158 parameters, eight pages, the thread-pool extension and
(D2) the instrument's input.

**Gate:** `scripts/check.sh` over `dev,asan,rtsan` passes; CI green on the head commit through the
draft PR; a fresh reviewer's final review and its fix wave done.

**Exit criteria** (the programme design's §6.1, each with its proof): both plugins expose four
layers and pass every validator (CI's validator steps); a four-layer render is bit-identical at
every block size and with the pool on or off (`[determinism]`); the 22 M1 goldens' hashes are
unchanged with layers B to D off (`scripts/update-goldens.sh`'s list in each commit); new goldens
cover a four-layer mix, a morph sweep, pitch spread and each mod source (the table above);
`gg-bench` gains `engine-layers` and `engine-layers-full`, report-only beside X (`[bench]`, and the
`release` run in the report); `engine-sample` shows no regressed ratio (CI's benchmark summary); the
Bitwig thread-pool check is recorded as the stand-in (`[plugin]`) with the in-host half named in
§22 for the close.

## 11. The listening pack: `docs/listening/m2-layers/`

Rendered for the record, listened to at sub-project 7 (programme design decision 1). A `README.md`
in the M1 packs' form (how to render; a table of files with length, what it is and what to listen
for; a host checklist for the close) and `specs/`:

| File | What | Listen for |
|---|---|---|
| `01-four-layers.wav` | the FX over a drum-like loop, four Live layers at distinct pitches and sizes, pad at the centre | four textures as one instrument; nothing louder than one layer was in M1 (D1) |
| `02-morph-square.wav` | the instrument, four Sample layers, the pad swept around the square over 8 s | smooth crossfades, no step at any corner or edge |
| `03-pitch-spread-ladder.wav` | one note, Pitch Spread 0, 2, 7, 12, 24 | from a line to a cluster; no clicks between steps |
| `04-layer-on-off.wav` | layer B switched on and off twice under layer A | the layer speaks at once when switched on; no click either way |
| `05-source-switch.wav` | the FX's layer A Live → Sample → Live over a loop | the switch itself is inaudible; the old grains finish |
| `06-lfo-position.wav` | LFO 1 → Position over a sweep, LFO 2 → Pitch | the scan moves smoothly; the pitch steps on the square |
| `07-envelope-notes.wav` | the envelope → Size and Level, three notes | each note swells and releases with Size and Level |
| `08-random-pitch.wav` | Random → Pitch at 4 Hz, Smooth 0 then 50% | stepped then slewed pitches, repeatable |
| `09-step-sequence.wav` | Step → Pitch, an 8-step figure at 4 Hz | the sequence, in time with itself |
| `10-wheel-and-bend.wav` | mod wheel → Size, pitch bend → Pitch over held notes | the wheel widens the grains; the bend bends |
| `11-follower-density.wav` | input bursts; the follower → Density | denser grains on each burst, thinning after |
| `12-transport-beat.wav` | 120 BPM; beat phase → Position | the scan locks to the beat |
| `13-macro-size-all-layers.wav` | Macro Size swept with one layer on, then with four | the macro moves every layer as one (decision 7) |

Thirteen specs; every render is deterministic, so the same commit gives the same bytes on one
platform. The README's host checklist (for the close's sitting in Live and Bitwig) covers: layers
B to D from the generic editor and the remote pages; a Bitwig project with four layers on,
`request_exec` seen under load (I4); the instrument's sidechain (D2) routed from a track; a
version-1 session opened in M2 sounding as it did; the migrated instrument's Macro 7.

## 12. The carry-over

**13 open rows at this sub-project's start** (`docs/m2-carry-over.md`: 51, 85, 87, 88, 118, 119,
144, 148, 184, 217, 218, 221, 232).

**Closed here:** **118** (the source-mode switch under the RT tripwire, both ways) and **119** (the
switch's hard cut; the grains now finish on their source), both by §6.4, since the Source-kind
parameter makes the switch reachable in both products in this wave. The programme design's §8
placed them in sub-project 2; this spec takes them, and the report says so.

**Touched files with open rows:** `engine/src/Engine.cpp` (118, 119: closed above),
`engine/include/gg/grain/GrainPool.h` (51: "with the overload ladder", sub-project 3's feature,
not a when-touched row), `core/include/gg/core/Processor.h` (184: M3's Voice scheduler). No other
open row names a file this sub-project edits.

**Expected at the end: 11 open rows,** no new rows. Every minor the final review raises is fixed in
the fix wave unless blocked outside our control or genuinely large (the policy).

## 13. INTENT changes (Part II, with one §23 row in the same change)

- **§14:** the layer (each with its pool, ring, diffuser, Level, Feedback and soft limiter; four
  pools of 4,096); the chain of §6.3 as built, with the morph law chosen in D1 and the on/off
  rule; the Source-kind parameter, its enumeration, fallback and kind-based Position, Spray and
  Feedback; Pitch Spread (D4) and the `PitchSpread` field; the mod matrix (sources, their
  determinism, targets, curves, the factory routes, slot order, the one clamp); Macro 7 as Feedback
  in both products and the fold; the enable gate's forced onset; notes acting on every layer.
- **§15:** C++23 and the GCC 13 floor (or the revert, with §22's reason); "Threading" marked as
  built: tasks per on layer, the runner, the host pool, the event buffering; the preset rename.
- **§16:** `engine-layers` and `engine-layers-full` named beside X as report-only cases.
- **§17:** the Perform view's instrument slot 7 shows "Macro Feedback"; the remote pages.
- **§18:** the ID table (§6.9) with the reserved blocks and the next-free indices; the patch at
  version 2, the `<mod>` element and the migration; the eight remote-controls pages; Feedback no
  longer hidden in the instrument; the three remaining product differences in metadata.
- **§19:** the thirteen goldens; the thread-pool invariance test; the event line's `"layer"`.
- **§22:** the C++23 row resolved (or its reason); the thread-pool row's in-host half pointed at
  the close; a new row for the per-field spread model if D5 keeps Pitch Spread alone.
- **§23 #65:** this sub-project's director decisions (D1–D5, with their dates), its rulings made
  during the build, and the §9 calls that amend Part II (the kind-based Position/Spray/Feedback,
  rows 118 and 119's move).

Part I is unchanged.

## 14. Risks

| Risk | Mitigation |
|---|---|
| The sub-project grows into half of M2 (programme design §10) | The plan groups the work in waves of parallel worktrees; the matrix's non-macro sources may split into a second PR under this sub-project if the plan exceeds M1's Perform UI |
| C++23 fails on a leg | The task's revert path (§6.1); nothing else depends on it |
| An `aumu` with an input bus fails auval or a host (D2) | The fallback inside the task: drop the port, option 2 holds |
| The factory routes differ from `macroOffsets` by a bit | The 10,000-vector equality test and the `fx-macros` hash, before `macroOffsets` is removed |
| A pool thread's floating-point mode differs from the audio thread's | Each task sets `ScopedDenormalsOff`; the thread-pool invariance test renders every golden-shaped case |
| The grain-event buffer overflows in a pathological piece | Sized at the pool's storage bound (2 × capacity), which bounds spawns per block; an assert in debug, a counted drop in release |
| Memory: four rings and four pools in every instance | About 45 MB at 48 kHz, recorded in §6.2 and INTENT §14; the cap is sub-project 2's |
| The parameter list (158) slows a host's scan or clutters its lists | Modules per layer; the remote pages; sub-project 6's views are the real surface |

## 15. Left to the builder

- How the flat slot space is generated from one table of layer parameters and one of globals
  (an X-macro or a constexpr table), so no name is typed four times.
- The kernel dispatch for a pool holding both sample-slot and ring grains, keeping spawn-order
  summation and the SIMD kernels' bit identity with their references.
- The exact JSON shapes of §6.11's keys, within what the section states.
- The LFO's sine polynomial, the envelope's segment arithmetic and the follower's exact
  coefficients, all libm-free and pinned by tests.
- Whether the morph weights are computed per sample from the ramped X and Y or the four weights
  are ramped themselves, under §6.3's requirement that one layer on gives exactly 1.0.
- The stub pool's implementation in the tests, and the fake host pool in the plugin harness.
- Which C++23 features the new code adopts beyond §6.1's list; the audio path adopts none in the
  first task.
- The remote pages' empty slots on the instrument (Mix stays empty, as M1).
- The listening specs' exact values, voiced so each file makes its point audible.
