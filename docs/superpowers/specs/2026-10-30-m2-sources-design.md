# M2 sub-project 2: Sources and persistence — design

| | |
|---|---|
| **Status** | **Draft for the programme manager's approval** (2026-10-30). §8 lists the forced choices for the director; nothing is built |
| **Milestone** | M2 Giga (INTENT §20, §23 #64), sub-project 2 of 7; wave 2, merged second (after 3, before 4) |
| **Spec authority** | `INTENT.md`, then `docs/superpowers/specs/2026-10-29-m2-programme-design.md` (its §6.2, §7 and §12 are this sub-project's charter: realized here, not reopened). This document records how this sub-project implements them |
| **Builds on** | M2 sub-project 1 as merged (`ab9f4e2`, PR #15; INTENT §23 #65): four layers each with a 16 s ring and two sample slots, the Source-kind parameter with `builtKind`, the per-layer `SourceExchange`, the 256-sample pieces and Feedback, the patch at version 2, the reserved index block `0x..40`–`0x..4F`; M1's `gg-media`, the `GGST` container, the loader and the search (§23 #48, #49, #53) |
| **Waits on** | Sub-project 3's file pyramid and per-grain level selection, for the ring pyramids alone (§6.9): the last tasks, built after rebasing onto the merged 3 |
| **Feeds** | Sub-project 4 (rebases onto this: the chain's tap point, `builtKind` with Resample built); 5 (`MEDI` v2's content-relative reference, the preset's `GGST` with no `AUDI`); 6 (the memory-cap readout, the per-layer loader strips, the freeze, record and capture gestures over this sub-project's operations and splices) |
| **Starts from** | `main` at `ab9f4e2`, and `docs/m2-carry-over.md` with **11 open rows** |

## 1. Why this sub-project

INTENT §5's distinctive promise is minutes-long live buffers inside a plugin, saved with the
session and managed with splices; §4 names the fourth source, resampling the instance's own output;
§8.8 says a project opened on another machine sounds identical. M1 built a 16 s ring that no
session keeps, a 600 s sample cap, one sample per instance, and a `MEDI` manifest with `AUDI`
reserved. Sub-project 1 made every layer a complete engine with its own ring and sample slots, and
defined the Source kind with Resample as an unbuilt value. This sub-project builds what those left
open, the programme design's inventory items I8 to I13 and I38's operations:

- **I8** the Resample source: a per-layer tap on the instance output, 256 samples late, that a
  layer granulates, giving the instrument a writable buffer and so Feedback on a Sample-default
  product;
- **I10** minutes-long rings and samples: the ring's length as a parameter, the 600 s cap gone;
- **I11** memory: a per-instance cap, saved with the state and shown by sub-project 6, and a
  process-wide sample pool deduplicating files across instances;
- **I12** persistence: `AUDI`, the 32 MB embed cap, the captures folder, the ring saved and
  restored so Freeze holds a restored ring (§23 #40 amended);
- **I13** collect and save;
- **I38** the ring's operations: overdub with a smoothed gain and crossfaded seams, a loop length
  within the ring, splices saved with the ring, and capture into an embedded or filed capture a
  Sample layer can load;
- and `MEDI` version 2, up to four samples each with its layer and a content-relative reference,
  this sub-project being M2's one adder of chunks (programme design §7.5).

I9 (carry-over rows 118 and 119) was closed by sub-project 1 (its spec §6.4) and is no longer
here.

**Headless by choice,** as sub-project 1 was: no new UI. The instrument's layer-A loader strip
stays as it is; layers B to D, the FX's samples, the ring's operations, splices and capture are
reached through the state, the host's generic editor, automation and `gg-render` until
sub-project 6 draws the lanes, the strips and the gestures.

## 2. Goals

1. **A session keeps its sound.** A project saved with a frozen ring of any length reopens, on the
   same machine or another with its captures folder, with that ring held, and renders
   bit-identically to the saved session (every grain and every sample of audio the same).
2. **Minutes, not seconds.** A layer's ring runs from 1 s to minutes long; a sample is bounded by
   memory alone; Position reaches the whole ring.
3. **The instance can eat itself.** A Resample layer granulates the instance's own output, in both
   products, so the instrument has Feedback and a buffer it writes.
4. **The ring is an instrument.** Loop, overdub, splices and capture work on every Live and
   Resample layer, deterministic on the control grid and without clicks.
5. **Memory is bounded and visible.** Every variable allocation counts against a per-instance cap
   that refuses deterministically with a status; one file loaded into ten instances is held once.
6. **Bit-identity with M1 at rest.** With Ring Length 16 s, Loop off, Overdub 0, no splice, no
   Resample layer, nothing embedded and the cap at its default, every one of the 22 M1 goldens
   keeps its WAV SHA-256, and so do sub-project 1's fourteen, except where the ring pyramids'
   level selection (§6.9) moves a case, named in its commit.
7. **No CPU regression on X**, and the new cases published beside it, report-only.

## 3. Non-goals and anti-goals

**Non-goals** (later sub-projects or milestones):
- The UI for any of it: the memory readout, the per-layer loader strips, the freeze, record,
  capture and splice gestures, drop onto a lane (sub-project 6); presets using the operations
  (5); the content folder's layout, the installed content path and the user content folder
  setting (5; this sub-project gives `MEDI` the reference and the search its hook, §6.7).
- The file pyramid and the kernels' per-grain level selection (3). This sub-project builds the
  ring's levels behind 3's accessor and reads nothing above level 0 until 3 has merged.
- The budget manager, quotas and the 16,384 pool (3); the Spectral and Oscillator kinds (4, M5),
  which keep falling back as sub-project 1 left them.
- Embedding user samples (INTENT §18: referenced, not embedded). Only rings and captures embed.
- Streaming from disk; a ring longer than memory allows.
- A looper transport (record arm, punch in, quantized loop start): M4's sync and 6's gestures.
  Here the operations are parameters and commands on the control grid.
- Per-layer or per-channel note routing (M3).

**Anti-goals** (a failure even if everything works):
- A state an M1 or sub-project-1 build can't skip past, or one this build can't load from them.
- A restore whose render depends on the machine, the codec's build or the host's block size.
- An allocation, lock, file read or unbounded copy on the audio thread: for a ring resize, a
  restore, a capture, a splice or a load.
- A resting preset, in either product, that renders differently from sub-project 1 by one bit
  (goal 6).
- A memory refusal that depends on timing, or a load that silently truncates a file.
- A click at a loop seam, an overdub's start, a splice change, a resize or a restore.
- An index outside `0x..40`–`0x..4F`, a new scope, a patch version bump, or a change to the
  container version.

## 4. Constraints

- CLAUDE.md's non-negotiables, the audio-thread rules first (this sub-project's main thread does
  more than M1's: every allocation, file and hash stays there); block-size invariance; 64-bit
  positions (ring time is int64, a 300 s ring at 192 kHz is 2^26 frames); randomness only from
  the counter RNG (the operations draw nothing); no behaviour that adapts to CPU or to the
  worker's speed (ruling R18 stands: a restore is synchronous).
- INTENT §8.3 (uniform source interface: Resample looks like Live to the grains), §8.6
  (deterministic), §8.8 (portable sessions) and §8.4 (real-time safety) shape the design; §8
  breaks ties.
- The programme design's §7: the chain and the tap 256 samples late, never the host block (§7.2);
  the ring length as a per-layer parameter in this block, bounded by the cap, Position keeping its
  seconds and M1 values with its CLAP range widened (§7.2); the container at version 1, this
  sub-project alone adding chunks, no patch bump, absent → default (§7.5); the `AUDI` criterion
  (§7.5); every M1 hash kept unless a commit says why (§7.6); packs for the record (§7.11).
- Ownership (§6.2): `engine/include/gg/source/` and its sources, `media/`, `SessionState.h`,
  `SourceExchange.h`, `plugin/src/SampleLoader*` and `SearchCache*`, the `Processor`'s source
  adoption and resample tap. Shared files (`Processor`, `ParamIds.h`, `ParamMap.h`'s
  `builtKind`, `tests/CMakeLists.txt`, the golden hash list, INTENT) by addition. Nothing in
  `ui/`, `engine/include/gg/grain/` or `core/src/space/`.
- Licensing: any new dependency pinned and AGPLv3-compatible. §6.6 adds none.
- The process: spec → plan → subagent-driven build in parallel worktrees → fresh review → fix
  wave; `scripts/check.sh` over `dev,asan,rtsan`; CI through a draft PR;
  `scripts/update-goldens.sh` before and after each DSP change; the carry-over counted at start
  and end; a rebase onto the merged sub-project 3 before the final verification.

## 5. Approaches

**The `AUDI` codec** (the programme design leaves it to this spec, §7.5). Two were weighed.

1. **Uncompressed float32, as canonical WAV bytes (chosen).** Each embedded payload is the bytes of
   a float32 WAV file, the same bytes the captures folder would hold. *Gains:* the restore is
   bit-exact, so a reopened session renders identically to the one saved, not merely within
   tolerance (goal 1, stronger than the criterion); the ring's floats, which may exceed ±1 (the
   ring clamps at ±2^20), need no scaling; one representation serves embedded and filed audio,
   `gg-media`'s decoder reads both, and any tool opens a capture; no new dependency. *Cost:*
   about 2.5× the size of 24-bit FLAC at ordinary levels, so the 32 MB embed cap holds about 83 s
   of stereo at 48 kHz instead of about 2 minutes; longer rings go to the captures folder sooner.
2. *FLAC at 24 bits through libFLAC.* *Gains:* smaller. *Costs:* lossy for floats (24-bit
   quantization after a per-payload scale, about −144 dBFS of error, within the goldens'
   tolerances but not bit-exact, so a restore would not reproduce the saved session's render
   exactly); a quantizer and scaler of our own to keep deterministic; a C library to pin, build on
   three platforms and notice.

INTENT §8.6 and §8.8 decide it: exactness is what the project promises, and the size difference
moves only where a long ring lives. §6.6 records the choice; INTENT §18's "as FLAC" and §15's
planned libFLAC row are amended (§13), and the programme design's exit "libFLAC is pinned" is
met by its reason: no FLAC encoder is needed. The programme manager confirms the amendment at this
spec's approval.

**Where the ring's length is decided.** The ring length is a CLAP parameter (programme design
§7.2), but its storage can't be allocated on the audio thread. Three ways:

1. **The main thread provides the storage; the audio thread adopts it (chosen).** A change of
   Ring Length is seen by the plugin's main thread (as it already watches Source for the text
   rescan), which allocates the new ring and copies the stable part of the window into it, then
   offers it; the audio thread adopts it at a call's start, copying only the newest frames,
   bounded, and the old storage goes back to be freed. *Cost:* the resize lands when the main
   thread is done, as a sample swap does (INTENT §14: "only a source given before the first call
   is part of a block-size-invariant render"); Ring Length is therefore not automatable (§6.3).
2. *Allocate every ring at the maximum.* Rejected: 300 s rings in four layers are about 0.9 GB at
   48 kHz before samples; the memory cap exists to prevent exactly that.
3. *Make Ring Length a per-instance setting outside the parameters.* Rejected by the programme
   design, which makes it a per-layer parameter so the state and `gg-render` carry it like any
   other.

**What Resample is to the engine.** A Resample layer is a Live-mode engine whose ring is fed by
the tap instead of the input (§6.2): the engine learns nothing new, the kernels are the live
kernels, and Position, Spray, Freeze, Feedback, Loop, Overdub and splices work on it unchanged.
The alternative, a third `SourceMode`, would duplicate the ring path for one difference of input.

**Where saved audio lives in the state.** One `AUDI` chunk holding every embedded payload with an
id, referenced from `MEDI` version 2's entries (§6.6), rather than one chunk per payload: the
container's reader rejects a repeated `PTCH` or `MEDI`, and one chunk keeps "exactly one of each
known ID" as the rule.

## 6. Design

### 6.1 The ring, generalized (I10, I38; INTENT §14 "Live ring buffer")

`LiveSource` keeps M1's contract (ring time from 0, the window `[R − N, R − 1]`, the admission
clamp, the 256-frame head-proximity fade, the 20 ms freeze ramp, ruling M1-o's storage) and gains
four things. Every one is a function of the sample clock and the parameter history, applied on the
control grid, and at its default is the identity on M1's bits.

- **Storage apart from the source** (§6.3): the frames live in a `RingStorage` the main thread
  allocates and the source points at; `prepare()` becomes "adopt this storage". Nothing else of the
  ring changes; a `RingStorage` holds the level-0 slots and the pyramid's levels (§6.9).
- **The readable window is `[R − W, R − 1]`,** with `W` the **window length** instead of `N`:
  `W = N` by default, `min(L, N)` while Loop is on with Loop Length `L`, and a splice's span
  while one is selected (below). The admission clamp and the fade use `W` wherever they used `N`,
  so a grain never reads outside the window and the window's two ends are crossfaded over 256
  frames, as the frozen ring's seam is today. A change of `W` applies at its grid boundary; a
  grain admitted under a wider window and now reading past the narrower one is taken out by the
  per-sample fade, as a freeze under a grain is (INTENT §14, "what admission can't foresee").
- **Overdub:** each frame written is `in[t] + o(t) · ring[R(t) − P]`, with `o(t)` the layer's
  Overdub gain ramped per sample over 10 ms like Feedback, and `P` the **period**: `min(L, N)`
  while Loop is on, else `N` (the frame leaving the window, still in storage: the slots hold
  `N + B` frames). The sum is stored as any input is (non-finite as 0, clamped to ±2^20). A piece
  whose Overdub ramp is 0 throughout writes the input alone, by the same path as today, so
  Overdub 0 is bit-identical to M1 (the sign of zero included), as Feedback 0 is. With Feedback
  too, the frame is `sanitize(in) + fb·limited[t − 256] + o·ring[R − P]`, in that order.
- **Splices:** up to `kMaxSplices` = 32 marks per layer, each a ring-time position, kept ascending
  in a fixed array on the audio thread. A mark older than `R − N` is dropped at the next write
  (it has left storage). Splice `k` ≥ 1 is the span from mark `k` to mark `k + 1`, or to the
  head for the last mark, counting from the oldest mark inside the window; while splice `k` is
  selected (the Splice parameter, §6.3), the window is that span: Position and Spray read seconds
  behind the span's end, and the fade crossfades the span's two ends. A selection past the count
  clamps to the last splice; 0 selects the whole window (the loop's, if Loop is on). Marks are
  added and cleared by commands on the grid (§6.4) and restored with the ring (§6.6).
- **Freeze** is unchanged: the head stops after the ramp, so a frozen ring with Loop on holds its
  last `L` seconds and with Overdub writes nothing (the write gain is 0; Freeze wins, as over
  Feedback). Unfrozen with Loop on and Overdub up, the ring is a sound-on-sound looper of period
  `L`: the input keeps entering, each pass decaying by `o` (D5).

### 6.2 The Resample source (I8; programme design §7.2)

- **The tap.** The `Processor` keeps the instance output, `out` as the host hears it (the mix with
  the dry signal and the reverb's return, and after sub-project 4's limiter once it lands; D4),
  for the last 256 samples per channel, by absolute time (`t mod 256`), as it keeps each layer's
  limited history. A Resample layer's ring input for sample `t` is `tap[t − 256]`, 0 before the
  first sample, in place of the instance input. Pieces are at most 256 samples and `out` of a
  piece is computed after its tasks join, so every sample a piece's tap needs was rendered before
  it: the same argument as Feedback's. One output channel: the mono output feeds both sides. The
  history is written only while some layer's kind is Resample; the first 256 samples after the
  first layer turns Resample read as silence (deterministic: the switch is on the grid). While no
  layer is Resample the tap costs nothing, and it never adds latency.
- **In the engine,** a Resample layer is a Live-mode engine: `builtKind(Resample, mode)` returns
  `SourceMode::Live` in both products, and the `Processor` routes the tap to its ring. Position
  and Spray read seconds behind the head; Freeze, Loop, Overdub, splices, capture and the pyramid
  work unchanged. Feedback follows the kind as sub-project 1 made it: it writes a Live or Resample
  layer's ring, so the instrument's Macro 7 and Feedback act on a Resample layer, the writable
  buffer the instrument lacked (its layers default to Sample). The text rule (FR-6) and the text
  rescan treat Resample as Live.
- **Stability:** the tap is bounded (`|out| ≤ 2 + |d|`), the ring clamps at ±2^20, Overdub ≤ 1,
  Feedback ≤ 0.95 and the soft limiter keeps `|limited| < 1`. A Resample layer at Pitch 0, Position
  256 samples behind and Overdub 1 is a comb that can grow by the layer's Level per pass; the
  `[slow]` test of §10 renders the worst case for 60 s and requires it finite and bounded, as
  Feedback's does.

### 6.3 Parameters: the reserved block (programme design §7.1, §7.2)

Five per-layer parameters at `0x..40`–`0x..44` in each of scopes 1 to 4, appended to
`GG_LAYER_PARAMS` as `GG_LAYER_PARAMS_M2_SOURCES`, displayed with the layer's letter, module
"Layer A" to "Layer D"; `0x45`–`0x4F` stay unused; scope 0's block takes nothing (the memory cap
is a setting, §6.5). No patch version bump: a state without them loads them at their defaults
(programme design §7.5).

| Index | Name | Range | Default | Flags |
|---|---|---|---|---|
| `40` | Ring Length | 1 s – `kRingMaxSeconds` (D3: 300 s), log (log2 s) | 16 s | not automatable, not modulatable (§5: the main thread resizes) |
| `41` | Loop | off/on | off | automatable, modulatable, stepped |
| `42` | Loop Length | 50 ms – `kRingMaxSeconds`, log; clamped to the ring's length in effect | 1 s | automatable, modulatable |
| `43` | Overdub | 0–100% (the gain `o`) | 0% | automatable, modulatable; ramped per sample |
| `44` | Splice | stepped 0–32 (0: the whole window) | 0 | automatable, modulatable, stepped |

- **Ring Length** is seen by the plugin's main thread, which resizes the ring (§6.5's exchange).
  Its CLAP value is log2 seconds, as Size's is. The ring in effect may lag the value, or stay
  where it was when the cap refuses (§6.5): the status says so, and sub-project 6 shows it.
  `gg-render` sizes every ring before the render from the spec's values.
- **Position's range widens** to the longest ring without moving any M1 value (programme design
  §7.2): the Live mapping `(16/255)(2^(8v) − 1)` seconds stays, and the CLAP maximum rises from
  1 to `vmax = log2(kRingMaxSeconds × 255 / 16 + 1) / 8` (1.528 for 300 s), where the mapping
  gives `kRingMaxSeconds`. A Sample layer's Position is still the fraction of its file, clamped
  to 1: above 1 the knob reads the file's end. The engine clamps a Position past the ring's
  window to the window, as its admission clamp always has. The M1 goldens' values are unchanged
  and so are their bits; the parameter's `max` changes, which is a range change before any
  freeze (the CLAP ID is the same, and hosts rescan on the next load).
- Loop Length, Overdub and Splice are routes' targets like any layer parameter (sub-project 1's
  rule: any parameter but a macro or a modulator's control); Ring Length is not a target.

### 6.4 Commands and capture (I38)

Two things a parameter can't carry: adding a splice mark, and capturing the ring.

- **Commands on the grid.** `Processor::process` gains a span of `CommandEvent`s (`offset`,
  `kind`, `layer`, `value`), merged with the parameter, note and control events by offset into
  the same boundary groups: `AddSplice` marks ring time `R(b) − value × sr` (value in seconds
  behind the head at the boundary `b`, clamped into the window; a mark on an existing one, or a
  33rd, is ignored) and `ClearSplices` empties the layer's marks. The plugin sends them through
  its editor queue (sub-project 6's gestures; the lossless UI → audio `SpscQueue`) and the audio
  thread applies them at the next boundary as it applies a parameter change; `gg-render` takes
  them in its `operations` list (§6.10). A command on a Sample layer is ignored.
- **Capture** reads a ring's window, or a splice of it, on the main thread, into a `SampleBuffer`
  at the host rate, which then goes wherever a loaded sample goes: a Sample layer's source
  (through the normal loader, so the pool and the cap apply), the session's `MEDI` as an
  embedded or filed capture (§6.6), or a file the user names. **The read is race-free without
  stopping the audio thread:** the audio thread publishes the head `R` with release order after
  each write; the main thread reads `R₀`, copies the frames `[R₀ − W, R₀)` from their slots, then
  reads the head again with acquire order and accepts the copy only if `R₁ − R₀ + W < C` (no
  frame written meanwhile shares a slot with one read), otherwise it copies again. A frozen ring
  passes at once; a live one passes unless the copy took longer than the ring is long. The
  capture is the window as of `R₀`, which is the last completed piece's end. Its marks inside the
  span come with it, as seconds from its start, for a capture that later becomes a ring (a
  restore of a captured file into a Live layer is a sample load, with no marks).
- **The plugin's API** (for sub-project 6's gestures and the tests): `captureRing(layer,
  spliceOrWhole) → std::shared_ptr<const SampleBuffer>`, `loadCapture(targetLayer, buffer)`, and
  `saveCapture(buffer, path)`; each main thread, none touching the audio thread beyond the
  exchange. In `gg-render`, `operations` of kind `capture` do all three in one step (§6.10).

### 6.5 Memory: the cap, the ring exchange and the pool (I10, I11; INTENT §14 "Memory management", §21)

- **The cap** is a per-instance setting, `memoryCapMiB`, saved with the state in a new chunk
  (`INST`, §6.6), default and ceiling per D2, shown by sub-project 6. It bounds the **variable
  allocations** of the instance: every layer's ring storage with its levels, every sample source
  the instance holds (planar, interleaved and sub-project 3's levels; the one playing, the one
  draining, one waiting and one offered each count), a capture in flight and a decoded buffer
  waiting for `activate()`. The fixed costs (the pools, the reverb, the event buffers, about
  65 MB at 48 kHz with sub-project 1's and 3's figures) are documented, not counted. A shared
  pool entry (below) counts in full in every instance that holds it: the cap is per instance, and
  conservative.
- **What the cap refuses, deterministically, with a status:** a load whose decoded size (from the
  header, with ruling R7's 64× trust for compressed files, then checked against what decodes), at
  the active rate and with its interleaved copy and levels, would take the instance past the cap;
  a Ring Length whose storage would; a restore whose rings or samples would (the state loads, the
  offending ring or sample is left out, and its status says *over the memory cap: needs X MB,
  Y MB free of Z*). The check happens before anything is allocated, on the main or loader
  thread; what is in memory is never evicted to make room. A refusal never depends on timing:
  the accounting is of what the instance references, not of what the OS has.
- **The 600 s file cap is gone** (`kMaxFileSeconds`): the cap's ceiling (D2) is the decoder's
  absolute bound, the per-instance cap the real one. Ruling R7's header trust, R8's truncation
  rules and the 2^40-frame ring guard stand.
- **The ring exchange** carries a `RingStorage` from the main thread to the audio thread, for a
  resize (Ring Length) and a restore (§6.6), through the layer's `SourceExchange` as one more
  thing an offer can carry beside a source and the values, so a restore's ring, sample and values
  land in one call. The main thread allocates the storage (level 0 and its levels), copies the
  **stable part** of the current window into it with the race check of §6.4 (frames older than
  the head as of `R₀`, at most the new window's length), fills the levels for what it copied, and
  offers it with `R₀`. The audio thread adopts it at a call's start: it copies the frames written
  since `R₀`, bounded by `kAdoptCopyFrames` = 8,192 (the newest 64 KB per channel), and their
  level frames, then swaps the pointer; the old storage goes back to be freed. An offer whose gap
  `R − R₀` exceeds the bound is declined and the main thread remakes it from the current head,
  which converges because the gap is what the main thread took to copy, at most one more round
  on any machine that keeps up with audio; a frozen ring has no gap. Grains sounding read the same
  frames from the new storage (the whole window was copied), so the swap is inaudible; a grain
  reading older than a shorter new window fades out by the window rule of §6.1. A restore's
  storage arrives filled, with the head at its frame count, so no copy is needed. The exchange
  holds at most one storage in flight per layer, so the audio side's hand-back ring is unchanged
  in size.
- **The process-wide sample pool** (`gg::media::SamplePool`, main and loader threads, one mutex):
  prepared sources by key `(sha256, size, rate)`, held weakly; `prepare()` consults it before
  converting and building, and shares a live entry. Ten instances of one project hold one copy of
  a shared sample's planar buffer, interleaved copy and levels. The pool holds nothing alive: the
  last instance to let go frees the entry. It follows `SearchCache`'s precedent (a process
  singleton, cleared by tests). The ring storages are per instance and never pooled.
- **Memory at 48 kHz, by example:** a 16 s stereo ring is 3 MB plus 3 MB of levels; 300 s,
  115 MB plus 115; a 10-minute stereo file 690 MB with sub-project 3's pyramid. Four 300 s rings
  with levels are 920 MB: above D2's recommended default, which is the point of the cap.

### 6.6 Persistence: `MEDI` version 2, `AUDI`, `INST`, the captures folder (I12; INTENT §18)

The container stays at version 1 (`GGST`, four chunks now: `PTCH`, `MEDI`, optionally `AUDI` and
`INST`); the patch at version 2; reading stays all or nothing for the known chunks, unknown ones
skipped. An M1 build loads such a state with its patch and "sample from a newer version"
(§23 #53), skipping `AUDI` and `INST`; a sub-project-1 build the same. This build loads M1's and
sub-project 1's states unchanged (a `MEDI` version 1 entry is layer A's).

- **`MEDI` version 2:** `<media version="2">` holding, in layer order, at most one `<sample>` per
  layer and at most one `<ring>` per layer.
  - `<sample layer="A|B|C|D" …>` keeps version 1's attributes (`sha256`, `size`, `path`,
    `relative`, `format`, `channels`, `rate`, `frames`, `embed`) and gains `content=""`, the
    content-relative reference (the file's path inside a content root, `/` separators, empty
    when the file wasn't loaded from one; sub-project 5's `ContentPaths.h` names the roots), and
    `audio=""`, the id of its `AUDI` payload when `embed="1"`. An embedded sample has no `path`
    or `relative` (a capture that never touched disk); a filed capture has its path in the
    captures folder. User samples are never embedded (INTENT §18): only captures and rings.
  - `<ring layer="A" frames="…" rate="…" head="…" loop="…" splices="s1 s2 …" sha256="…"
    size="…" embed="1" audio="n" | path="…">`: the window saved as float32 WAV bytes
    (oldest frame first), `rate` the host rate it was written at, `splices` the marks as frames
    from the window's start, embedded or filed by the embed rule below. Which rings are saved is
    D1. The ring's frozen state is the layer's Freeze parameter in `PTCH`; Loop, Loop Length,
    Overdub and Splice are parameters too.
  - The attributes' escaping, the 64-deep pre-scan, the NUL rule and the all-or-nothing reading
    are version 1's. A `<sample>` or `<ring>` with a malformed attribute fails the state, as a
    malformed entry does today (ruling R9's writer rule holds: `encodeState` never writes an
    entry that wouldn't decode).
- **`AUDI`:** one chunk, a list of payloads: `u32 count`, then per payload `u32 id`, `u64
  length`, the bytes; each payload is a complete float32 WAV file (RIFF, `WAVE_FORMAT_IEEE_FLOAT`
  or the extensible form for stereo, 48-byte header, data), the same bytes a filed capture has,
  decoded by `gg-media`'s `decodeAudioFile` from memory (a `decodeAudioBytes` entry over
  dr_wav's memory reader, new). Its `sha256` and `size` in `MEDI` are of those bytes, so an
  embedded payload and its filed twin have one fingerprint and the search finds either.
- **The embed rule** (INTENT §18's 32 MB): at save, the rings and captures to persist are taken in
  layer order, rings before captures within a layer; each is embedded while the `AUDI` total with
  it stays at or under `kEmbedCapBytes` = 32 × 2^20, and otherwise written to the **captures
  folder** as `<sha256>.wav` (skipped when a file of that name and size exists) and referenced by
  path. So a session's embedded audio never exceeds 32 MB and the state stays under the 64 MiB
  read cap. A write to the captures folder that fails leaves the entry out and the status *not
  saved: captures folder unwritable*; the rest of the state saves (R9's spirit). The fingerprint of
  a frozen ring is cached by (layer, head, write epoch), so a host that saves every few seconds
  hashes an unchanged ring once.
- **The captures folder** is a per-machine setting, `capturesFolder` in `settings.xml` (absent →
  the default of D7), created on first use. A restore finds a filed ring or capture by the search
  of §6.7, the captures folder being one more candidate root, so a project moved with its
  captures folder reopens whole.
- **`INST`:** `<instance version="1" memoryCapMiB="1024"/>`, the per-instance settings; absent
  → defaults; a newer `version` loads its known attributes and keeps nothing (unlike `MEDI`, it
  references no media). Extensible for later per-instance settings.
- **Restore** (D8 stands: synchronous, in `stateLoad`): the main thread decodes each saved ring
  into a `RingStorage` of the layer's Ring Length (the ring's frames at the saved rate, converted
  with r8brain if the host rate differs, as a sample is; a ring longer than the new length keeps
  its newest frames) with its levels and marks, each sample through the loader as today, and
  offers them with the values per layer, so the first `process()` after opening renders with
  every ring and sample in place, bit for bit what the saved session rendered from that point
  (goal 1; the clock restarts at 0, as any reopened session's does, and the grain index with it).
  Inactive, the storages wait for `activate()` as a decoded sample does. The ring exchange's
  one-in-flight rule holds: a restore replaces an unadopted resize.
- **Freeze holds a restored ring** (§23 #40 amended): a layer whose ring was restored arms at the
  load, so a Freeze that is on freezes at once and the restored ring is held from the first
  sample; a layer whose ring wasn't saved (an M1 state, a ring not saved under D1) keeps "live
  until toggled", ruling FR-8 unchanged. So a reopened frozen pad is held, which #40 deferred to
  this sub-project.

### 6.7 Finding media (INTENT §18 "Finding a missing sample"; I13)

- **The candidates,** in order, each matched by size then SHA-256 as today: 1 the absolute path;
  2 **the content roots plus the content-relative reference**, when the entry has one (the order
  of roots is sub-project 5's `ContentPaths.h`; `findSample` takes them as an argument, empty
  until 5 wires it, and `gg-render` passes `--content-dir`); 3 the sample folder plus the
  relative path; 4 **the captures folder** plus the file's name, for an entry whose path is in a
  captures folder; 5 a file of the same name in the saved path's folder, the last load folder,
  then under the sample folder (the 50,000-entry scan). `SearchCache` remembers misses as today,
  keyed also by the content roots and the captures folder.
- **Collect and save** (`gg::media::collectMedia`): given a session state and a folder, copies
  every referenced file (samples, filed rings and captures) that can be found into the folder,
  named `<name>` or, on a name clash with different content, `<name>-<sha256 prefix>`, verifies
  each copy's hash, and returns the state with each entry's `path` pointing into the folder and
  `relative` recomputed, with a list of what couldn't be found. The plugin offers it as a main
  thread command (`collectAndSave(folder)`) that then marks the state dirty; sub-project 6 wires
  the button; the tests drive the function and the command. Embedded audio needs no collecting.

### 6.8 The loaders: four per instance, in both products

`SampleLoader` becomes one per layer, in both products (an FX layer set to Sample reads its
sample; the FX plugin gains the loaders it never had), each with its layer's exchange, each
recording its `MediaEntry` with its layer and content reference, and `restore` taking the whole
manifest (every entry and ring), the loaders' restores run in layer order. The instrument's
layer-A strip (D3 of M1's Sources) drives loader A as before; nothing else in the editor changes.
The search cache and the pool are shared. The FX writes a `MEDI` with its entries now, no longer
empty by construction (an FX with no sample still writes an empty one).

### 6.9 The ring pyramids (I16's second half; after sub-project 3 merges)

- **Behind 3's accessor:** `LiveSource::levelCount()` and `level(n)` return the ring's levels, each
  a `RingView` of a decimated ring with its stride, built by 3's half-band FIR (its named taps,
  so a level of a ring holds the same bits a file of the same frames would get) and decimated by
  2 per level, kept in the `RingStorage` beside level 0, each level's slots `ceil(C / 2ⁿ)` long.
- **Incremental:** the levels are written as level 0 is, inside `write()`: level `n`'s frame `j`
  is computed when level `n − 1` holds its taps, so each level lags the head by the filter's
  group delay at its rate. The cumulative lag of level `n` in level-0 frames is `(2ⁿ − 1) × h`
  with `h` the filter's half length; the ring builds the levels whose lag stays at or under the
  head margin `M` = 256 (with 3's 31-tap design, `h` = 15, all of `kMaxLevel` = 4's five levels:
  225 ≤ 256), so every level is complete over any sample's window, which the admission clamp
  keeps `M` behind the head. Frozen, the levels freeze with level 0. A write costs `Σ 1/2ⁿ × h`
  multiplies per frame, about 15, less than one grain's.
- **A resize or restore** fills the levels of the copied frames on the thread that copies (the
  main thread for the stable part, the audio thread for the bounded newest part), by the same
  function, so a restored ring's levels are bit for bit the ones the saved ring had.
- **Only HQ grains read above level 0** (3's D5), so no M1 or sub-project 1 golden moves; 3's
  `fx-hq-live-level0`, which pins a Live HQ grain at level 0 stretched, is regenerated here as
  its spec names, and renamed `fx-hq-live` in the same commit. Capture takes level 0 alone; a
  captured sample gets 3's file pyramid when it is loaded.

### 6.10 `gg-render` (programme design §6.2: "`gg-render` loads `AUDI` from a spec")

- **Parameters by name:** `"A Ring Length"` (log2 seconds) and the four others, in `params` and
  `automation`; the rings are sized before the render.
- **`state`:** a path to a `GGST` file, loaded before the render as a plugin's `stateLoad` would
  (`PTCH`, `MEDI`, `AUDI`, `INST`), rings restored, samples found through `--content-dir` and
  the spec's directory (never searched by name), the values applied; `params` then override.
  `--save-state out.ggst` writes the session state at the render's end (the rings per D1's rule as
  if saved by a plugin, embedded or, with `--captures-dir`, filed). The golden media writer
  (`--write-media`) makes `frozen-ring.ggst` and `four-samples.ggst` this way, so the restore
  goldens need no committed binary.
- **`operations`:** a time-ordered list of `{"time", "layer", "op", …}`: `"splice"` (a mark
  `"behind"` seconds behind the head; default 0), `"clearSplices"`, and `"capture"` (`"splice"`
  0 for the whole window, `"into"` a layer letter whose sample it becomes, and optional `"file"`
  to write it). `gg-render` cuts its process calls at each operation's frame, so a capture lands
  at that sample whatever the spec's block size and the determinism test can include one; a
  splice or clear is a command event at its offset.
- **`memoryCapMiB`** (optional, top level) sets the cap, so a refusal is testable.
- Unknown keys stay errors; sample mode takes none of these.

### 6.11 `gg-bench`

One case, report-only beside X (programme design §7.6): **`engine-live-long`**, `engine-live`'s
stream over a 300 s ring instead of 16 s, with its positions spread over the ring (Spray the
ring's length), so the cache behaviour of a minutes-long buffer is measured; its baseline
entries come from `--make-baseline --merge` after three full CI runs, at the close. The ring's
write with the levels on is measured by `engine-live` itself once the pyramid lands (its ratio
moves by the levels' cost, named in that commit); `engine-sample`, X, is untouched by anything
here.

## 7. Determinism and real-time safety

- **Everything audible is on the grid or at a call's start.** Loop, Loop Length, Overdub, Splice,
  the Source kind, Feedback and the commands apply at grid boundaries; the tap is by absolute
  time; so a render with any of them is bit-identical at block sizes 1, 32, 64, 511 and 4096 and
  under random splits (the `[determinism]` tests gain a Resample layer, a loop with overdub,
  splices and a `gg-render` capture). A ring resize and a restore land at a call's start, as a
  sample swap does, and so belong to a block-size-invariant render only when made before the
  first call (INTENT §14's existing rule); `gg-render` makes them there.
- **Nothing draws:** no operation uses the RNG; the sources' and the grains' draws are unchanged,
  so every pre-existing event log holds (goal 6).
- **The audio thread never allocates, locks, reads a file or copies without a bound:** a resize's
  audio-side copy is at most `kAdoptCopyFrames`; a restore's storage arrives filled; a capture
  reads on the main thread with the wrap check; splices are a fixed array; the tap is 256 × 2
  floats; the loaders, the pool, the cap's accounting, the hashing and the captures folder are
  main or loader thread. The `[rt]` tests put the tripwire on: a Resample layer's piece, a loop
  with overdub, a splice command, a ring adoption with the bounded copy, and `process` and
  `params.flush` with four loaders offering; RTSan runs the same under `rtsan`.
- **The restore is exact:** float32 payloads, levels rebuilt by the same function, the marks as
  frames, the head at the frame count, Freeze armed; the main-thread conversion of a ring saved at
  another rate is r8brain's, deterministic on one platform, with the event logs exact across
  platforms as for a converted sample.
- **The cap is a pure function** of what the instance references and the entry's declared size,
  never of the OS or of timing.

## 8. Decisions for the director

Each a forced choice; the recommended option is first, with one line on what each costs. Nothing
engineering-only is here (that is §9).

**D1. Which rings a session saves** (§6.6; what "the ring saved and restored" means for a ring
that isn't held).
1. **A ring is saved when it is held: frozen, or Loop on.** A plain live ring refills from the
   input and isn't saved. *Cost:* a project reopened with an unfrozen Live layer starts with an
   empty ring, as today, so Position reads silence until it refills (16 s by default); Freeze on
   such a layer stays live until toggled.
2. *Every Live and Resample layer's ring is saved.* *Cost:* up to four rings of audio per
   instance in every save, most of it transient; a 16 s stereo ring is 3 MB, a minute 11 MB, so
   projects grow and the 32 MB embed cap is reached by rings alone, after which saves write files.
3. *No ring is saved; only captures persist.* *Cost:* a reopened frozen pad is not held, which is
   what §23 #40 deferred to this sub-project to fix; the exit criterion "a frozen minutes-long
   ring reopens" isn't met.

**D2. The memory cap's default and ceiling** (§6.5; shown by sub-project 6).
1. **Default 1 GiB, ceiling 16 GiB, floor 64 MiB.** *Cost:* four 300 s rings with levels (about
   0.9 GB) plus any sample exceed the default, so a user who wants four maximal rings raises the
   cap once per instance.
2. *Default 2 GiB, ceiling 16 GiB.* *Cost:* a project of ten instances may claim 20 GB before any
   cap bites; laptops swap.
3. *Default 512 MiB, ceiling 8 GiB.* *Cost:* one 300 s ring with its levels (230 MB) and a
   5-minute stereo sample with its pyramid (345 MB) already exceed it; the cap is raised often.

**D3. The longest ring** (`kRingMaxSeconds`, §6.3; also the top of Loop Length and of Position's
widened range).
1. **300 s (5 minutes).** *Cost:* 115 MB plus 115 of levels per stereo ring at 48 kHz; Position's
   CLAP range tops at 1.528.
2. *600 s (10 minutes), M1's sample cap.* *Cost:* 460 MB per ring with levels; four of them don't
   fit D2's recommended default twice over; Position's log mapping gives the last minutes a sliver
   of the knob.
3. *120 s (2 minutes).* *Cost:* "minutes-long" by a hair; a longer ring is a later range change.

**D4. What the Resample source hears** (§6.2).
1. **The instance output as the listener hears it:** the mix with the dry signal (FX), the
   reverb's return and, from sub-project 4, the limiter; INTENT §14's words. *Cost:* in the FX at
   Mix 50% a Resample layer re-grains the dry input too, and the reverb's tail recurses.
2. *The layers' weighted sum before the mix and the reverb* (grains alone). *Cost:* "resampling
   its own output" no longer means the output; the reverb never recurses, which some will miss.

**D5. What Loop does while the ring isn't frozen** (§6.1).
1. **A sound-on-sound looper:** the window is the last Loop Length seconds, the input keeps
   entering, and Overdub recycles each frame leaving the window at its gain, so the loop decays by
   Overdub per pass (0: a plain delay-length window; 100%: it never decays). Freeze then holds the
   loop. *Cost:* Loop alone (Overdub 0) only narrows the scan window; the "loop" is heard only
   with Overdub up or Freeze on.
2. *Loop on stops the head, as Freeze does,* and Overdub mixes new input into the held loop.
   *Cost:* two ways to stop the head, and an unfrozen Overdub needs a second write path over a
   stopped head; the looper's decay-per-pass is lost.

**D6. How splices are numbered** (§6.1; the Splice parameter's values).
1. **From the oldest mark inside the window:** splice 1 is the oldest span, the last the newest.
   *Cost:* on a live (unfrozen) ring the numbering shifts as marks age out; on a held ring, the
   normal case, it is stable and automatable.
2. *From the newest:* splice 1 is the most recent cut. *Cost:* every new mark renumbers the rest,
   so a Splice value in automation or a preset points elsewhere after a mark is added.

**D7. The default captures folder** (§6.6; `capturesFolder` in `settings.xml`, settable).
1. **The user's Music folder: `~/Music/Giga Grains/Captures`** (Windows `%USERPROFILE%\Music\Giga
   Grains\Captures`, Linux `$XDG_MUSIC_DIR` or `~/Music`). *Cost:* a second folder beside the
   sample folder to carry when a project moves (collect and save gathers both).
2. *Inside the sample folder, `<sample folder>/Captures`,* falling back to option 1 while no sample
   folder is set. *Cost:* captures land among the user's library, and move when the user changes
   the sample folder setting.

## 9. My calls

Engineering choices, each with its reason.

- **`AUDI` payloads are float32 WAV bytes, and libFLAC isn't added** (§5): the restore is exact,
  the ring's ±2^20 floats need no scaling, one representation serves embedded and filed audio, and
  a dependency that only encodes isn't worth building on three platforms. INTENT §15's planned row
  and §18's "as FLAC" are amended (§13); the programme design's exit "libFLAC is pinned" is met by
  not needing it, which the programme manager confirms.
- **One `AUDI` chunk with ids, not a chunk per payload** (§5): the container's rule stays "one of
  each known ID".
- **The memory cap in an `INST` chunk,** not in `PTCH`: no patch change (sub-project 1's is M2's
  one bump), and it is a setting, not a parameter (a host shouldn't automate memory).
- **Ring Length is resized by the main thread and isn't automatable** (§5, §6.3): allocation can't
  happen on the audio thread, and a parameter a host can't automate can't thrash it; the resize
  keeps the whole window by splitting the copy into a stable main-thread part and a bounded
  audio-thread part, so a resize is inaudible rather than a restart.
- **Resample is a Live-mode engine fed by the tap** (§6.2): one ring code path; the tap's history
  is kept only while a Resample layer exists, so the claim "costs nothing otherwise" is exact.
- **The window length `W` generalizes `N`** (§6.1): loop and splice are one mechanism, and the
  existing fade and admission clamp give the seams and the no-click property for free.
- **Overdub's period is the window with Loop on, else the ring** (§6.1): Overdub without Loop is
  then a 16 s (or longer) sound-on-sound, consistent rather than a dead control.
- **Splices are 32 fixed marks in ring time, added by grid commands** (§6.1, §6.4): no allocation,
  block-size invariant, and the marks age out with the frames they mark.
- **Capture reads on the main thread with a wrap check** (§6.4): the audio thread does nothing,
  the read is race-free by construction, and a frozen ring (the normal case) passes at once.
- **The ring pyramids use 3's filter and fit under the head margin** (§6.9): a ring's level equals a
  file's for the same frames, and every level is complete wherever a grain may read, so no grain
  ever waits for a level.
- **The pool is keyed by (fingerprint, rate) and holds weakly** (§6.5): a cache that keeps memory
  alive would fight the cap; the instances' own references decide lifetime.
- **The cap counts the instance's references in full, shared or not** (§6.5): a per-instance cap
  that depended on other instances would refuse differently from one session to the next.
- **The 600 s cap is removed outright** rather than raised: the cap's ceiling bounds the decoder,
  and two limits would need two messages.
- **`findSample` takes the content roots as an argument** (§6.7): sub-project 5 owns where content
  lives; this sub-project owns how a reference is matched.
- **Collect and save is a `gg-media` function plus a plugin command** (§6.7): testable headless,
  and sub-project 6 wires one button.
- **Four loaders per instance in both products** (§6.8): the FX gets `MEDI` entries, as the
  programme design says, and layer A's strip keeps working.
- **A restored ring arms Freeze; an unsaved one doesn't** (§6.6): the one case #40 deferred is
  fixed, and FR-8's rule is untouched where no ring came back.
- **`gg-render` cuts its calls at an operation's frame** (§6.10): a capture then has a sample
  position, and the determinism test can include one.
- **The restore goldens' state files are generated by `--write-media`,** never committed.
- **One benchmark case, `engine-live-long`** (§6.11): the long ring's cache cost is the one new
  thing X-like timing can show; the tap and the operations are per-sample arithmetic the
  `Processor`'s unmeasured cost already contains, for sub-project 7's idle run.
- **One PR** (the brief), with the pyramid tasks last after the rebase onto 3; the programme
  design's §10 split into two PRs is kept as the fallback if the plan exceeds M1's Perform UI,
  decided at the plan with the programme manager.

## 10. Verification

**Unit tests** (Catch2, labels in brackets):
- `[engine][live]`: the window `W` (Loop on narrows it; the admission clamp and the fade use it;
  a grain past a narrowed window fades to 0 with no read outside it); Overdub (0 bit-identical to
  M1's write; the period with Loop on and off; the ramp; the clamp at ±2^20; with Feedback, the
  order of the sum); splices (add, clear, aging out, the 33rd ignored, the span's ends, Splice past
  the count); the storage swap (the window preserved frame for frame, longer and shorter, frozen and
  live; grains sounding unchanged through it); the levels (3's filter bit for bit against a file's
  pyramid of the same frames; complete to `head − M` at every level; frozen with level 0; rebuilt
  identical after a swap).
- `[core][source]`: the tap (256 late by absolute time; silence before the first; one channel;
  written only while a Resample layer exists); a Resample layer's Feedback; `builtKind(Resample)`
  in both products; the commands on the grid; a capture's wrap check (a mocked head that wraps
  forces a retry); the `[slow]` worst-case Resample loop for 60 s, finite and bounded.
- `[core][patch]`: the five parameters pinned by (name, ID) in every layer scope, inside the
  block, `kNextFree*` unchanged; Position's `vmax`; a state without them loads them at their
  defaults; a version-2 patch unchanged.
- `[core]` state: `MEDI` v2 round trip (four samples with layers, content references, embedded and
  filed captures, rings with marks); `AUDI` payloads' WAV bytes decode to the frames written; the
  embed rule at the 32 MB boundary (just under embeds, over files); `INST` round trip, absent →
  default, a newer version's known attributes; a v1 `MEDI` loads as layer A's; a malformed
  `<ring>` fails the state; ruling R9 for an unwritable entry; `decodeAudioBytes`.
- `[media]`: the pool (two loads share; the last reference frees; different rates don't share);
  the cap's accounting and refusals (a load, a Ring Length, a restore; the status text; the check
  before allocation); the 600 s cap gone (a 20-minute generated file loads under a large cap and is
  refused under a small one with the status); `findSample`'s content roots and captures folder in
  order; `collectMedia` (copies, renames on clash, rewrites paths, lists the missing); the
  settings' `capturesFolder`; the ring's rate conversion on restore.
- `[plugin]`: four loaders in both products; the FX saves and restores a layer-B sample; a state
  with rings and captures round-trips through the CLAP state extension and renders bit for bit
  after the restore (two restores identical; restore then render equals the saved session's
  render from the save point); Freeze on a restored ring holds from the first sample, on an
  unsaved one stays live until toggled; `captureRing`, `loadCapture`, `saveCapture`,
  `collectAndSave`; a Ring Length change resizes through the exchange while processing; the cap
  refuses a load with the status; `process` and `params.flush` under the tripwire with the above.
- `[determinism]`: block-size invariance of renders with a Resample layer, a loop with overdub,
  splices and a `gg-render` capture; the thread-pool invariance test extended with a Resample
  layer.
- `[rt]`, `[rtsan]`: §7's list.
- `[render]`: the new keys parse and reject; `state`, `--save-state`, `operations`, the call cuts;
  `memoryCapMiB`.
- `[bench]`: `engine-live-long` builds and runs `--quick`.
- `[golden][metrics]`: each new case catches all sixteen injected clicks or is pinned with its
  reason.

**Goldens** (7 new, 43 in all with sub-project 3's; every M1 and sub-project 1 WAV keeps its
SHA-256, stated per commit; 3's `fx-hq-live-level0` regenerated as `fx-hq-live` by the pyramid
task, named in its commit):

| Case | Mode | What |
|---|---|---|
| `inst-resample-feedback` | instrument | layer A Sample over a 220 Hz sine, one held note; layer B Resample at Pitch +12, Feedback 0.6: recursive granulation of the instrument's own output |
| `fx-resample` | fx | layer A Live over a sweep; layer B Resample at Pitch −12, Position 0.5 s: the output re-grained an octave down |
| `fx-long-ring` | fx, 24 s | Ring Length 60 s; an 8 s burst input, then Position stepped to 10, 18 and 22 s behind at 12, 18 and 22 s: reading minutes-class history past 16 s |
| `fx-loop-overdub` | fx | Loop on, Loop Length 1 s, Overdub 0.7 over a 2 s noise-burst pattern; Freeze at 4 s; Overdub to 0 at 6 s |
| `fx-splice-capture` | fx | Freeze at 2 s; splices at 1.5 and 0.5 s behind; Splice 1 at 2.5 s, 2 at 3.5 s; capture of splice 2 into layer B at 4 s, layer B Sample at Pitch +7 |
| `fx-ring-restore` | fx | `state: frozen-ring.ggst` (a generated 20 s ring frozen with two splices, embedded) restored with Freeze on; 4 s of scanning: the restore gate |
| `inst-four-samples` | instrument | `state: four-samples.ggst`, four Sample layers by content-relative reference under `--content-dir`, one held note, the pad at the centre: `MEDI` v2 and the content reference |

**Benchmarks:** `engine-live-long` from `release` with its figures in the report; `engine-sample`
shows no `regressed` ratio on any CI model at the end (programme design §7.6); `engine-live`'s
ratio after the pyramid task named in its commit.

**Validators:** clap-validator, pluginval, auval (three identities) and Steinberg's validator on
`release`, on every CI leg, with the new parameters and Position's widened range.

**Gate:** `scripts/check.sh` over `dev,asan,rtsan`; CI green on the head through the draft PR; a
fresh reviewer's final review and its fix wave; the rebase onto the merged sub-project 3 and the
same again.

**Exit criteria** (programme design §6.2, each with its proof): a session with a frozen
minutes-long ring reopens and renders bit-identically on another machine with its captures folder
(`fx-ring-restore`'s cross-platform event log and metrics, the `[plugin]` restore tests, the
filed-ring round trip); a 16 s ring at Freeze renders bit-identically to M1 when nothing is
embedded (the hash list: `fx-feedback` and `fx-note-freeze` unchanged); the memory cap refuses a
load deterministically with a status (`[media]`, `[plugin]`); the RT tripwire covers the mode
switch both ways (sub-project 1's test, kept, plus the Resample switch added to it); libFLAC
(§9: not needed, by the programme manager's confirmation); new goldens cover resample feedback in
the instrument, a restored ring and a four-sample instrument (the table); `gg-render` loads `AUDI`
from a spec (`state`, `fx-ring-restore`).

## 11. The listening pack: `docs/listening/m2-sources/`

Rendered for the record, heard at sub-project 7 (programme design decision 1). A `README.md` in
the M1 and sub-project 1 packs' form (how to render; a table of files with length, what it is,
measured peak and RMS, and what to listen for; a host checklist for the close) and `specs/`:

| File | What | Listen for |
|---|---|---|
| `01-resample-feedback.wav` | the instrument, a note over a sine, layer B Resample +12 with Feedback rising 0 → 0.8 over 12 s | the instrument eating itself: a rising cascade of octaves that stays bounded |
| `02-fx-resample-octave.wav` | the FX over a sweep, layer B Resample −12, then −24 at 5 s | the output re-grained down, the reverb's tail recursing (D4) |
| `03-long-ring-scan.wav` | a 120 s ring, 90 s of a sparse loop, then Position swept from 0 to 80 s behind over 40 s (the file is 130 s) | scanning back over a minute and more without a seam or click |
| `04-loop-overdub.wav` | Loop 2 s, Overdub stepped 0, 0.5, 0.8, 1.0 under a rhythmic input, 6 s each | the sound-on-sound building, decaying faster at lower Overdub, holding at 1 (D5) |
| `05-frozen-loop.wav` | the ring frozen at 4 s with Loop 1 s on, scanned by Spray | the held loop's seam crossfaded, no click |
| `06-splices.wav` | a frozen 8 s ring cut into four splices, Splice stepped 1 to 4 every 2 s, then 0 | each span alone, then the whole; no click at a change (D6) |
| `07-capture-to-layer.wav` | splice 2 of `06` captured into layer B at Pitch +7 while layer A keeps scanning | the capture plays as a sample under the ring |
| `08-ring-restore-a.wav` / `08-ring-restore-b.wav` | `a`: a frozen 20 s ring scanned for 6 s, state saved at 0 s of the scan; `b`: that state restored and the same 6 s rendered | identical files (the SHA-256s are in the README): the reopened pad is the saved one |
| `09-resize-live.wav` | a 16 s ring resized to 60 s at 5 s and back to 8 s at 15 s while scanning 3 s behind | nothing audible at either resize |
| `10-fx-sample-layer.wav` | the FX's layer B loaded with a file through `MEDI` while layer A grains the input | a sample layer inside the effect |

Ten items, twelve files; every render is deterministic, so the same commit gives the same bytes on
one platform. The README's host checklist (for the close's sitting in Live and Bitwig) covers:
save a project with a frozen ring, close and reopen it, and hear it held (Freeze on, in VST3 and
AU); move the project and its captures folder to the other machine and reopen; a ring longer than
32 MB going to the captures folder; set a Ring Length of 2 minutes from the generic editor and
hear no cut; a Resample layer with the instrument's Macro 7; the cap refused with its status
(through sub-project 6's readout once it lands); the FX restoring a sample on layer B from a
saved state.

## 12. The carry-over

**11 open rows at this sub-project's start** (`docs/m2-carry-over.md`: 51, 85, 87, 88, 144, 148,
184, 217, 218, 221, 232). Rows 118 and 119, once placed here, were closed by sub-project 1.

**Closed here:** none is this sub-project's. Row 51 is the overload ladder's (3); 184 names
`Processor.h` but waits on M3's Voice scheduler (a feature, not a when-touched row); 217 and 221
are sub-project 6's; 85, 87, 88, 218 and 232 wait on upstream; 144 and 148 on 1.0.0.

**Touched files with open rows:** `core/include/gg/core/Processor.h` (184, above). No open
"when touched" row names a file this sub-project edits.

**Expected at the end: 11 open rows,** no new rows. Every minor the final review raises is fixed
in the fix wave unless blocked outside our control or genuinely large (the policy).

## 13. INTENT changes (Part II, with one §23 row in the same change)

- **§14:** "Loading": the 600 s cap replaced by the memory cap; "Memory management" as built (the
  cap, what it counts, the pool); "Live ring buffer": the length as a parameter, the window, Loop,
  Overdub, splices, capture, the resize's exchange and bounded copy; "Mix and global FX": the tap
  as built, what it hears (D4); "The Source kind": Resample built in both products; "Feedback": on
  a Resample layer; "Freeze is live until toggled": amended for a restored ring (#40); "Mipmaps":
  the ring's incremental levels as built.
- **§15:** the libFLAC row removed from the planned dependencies, with the reason; the pool under
  `media/`; no new dependency.
- **§16:** `engine-live-long` beside X, report-only; `engine-live`'s figure with the levels.
- **§18:** `AUDI` as float32 WAV payloads (not FLAC), the embed rule, the captures folder (D7),
  `MEDI` version 2 in full, `INST`, collect and save, the search order with content roots and the
  captures folder; the five parameters at `0x..40`–`0x..44` in the table; Position's widened range;
  "Captures and freezes are embedded up to 32 MB" as built; what a session saves (D1).
- **§19:** the seven goldens; the determinism tests' new renders; `gg-render`'s `state`,
  `--save-state` and `operations`.
- **§20:** nothing (the M2 row already names this sub-project).
- **§21:** the memory risk's mitigation as built.
- **§23 #66:** this sub-project's director decisions (D1–D7), the codec call and the libFLAC
  amendment, the §9 calls that amend Part II (the tap's content, the resize through the main
  thread, Ring Length not automatable, the restored ring arming Freeze), and the rulings made
  during the build.

Part I is unchanged: §4's resample source and §5's minutes-long buffers "saved with the session
and managed with splices like Morphagene" are what this sub-project builds.

## 14. Risks

| Risk | Mitigation |
|---|---|
| The sub-project is the programme's largest after 1 (programme design §10) | The plan's waves run in parallel worktrees; the pyramid tasks are last and small; the two-PR split is the fallback (§9) |
| A host saves state every few seconds and a 100 MB frozen ring is hashed and written each time | The fingerprint cache by (layer, head, epoch); a filed ring is skipped when its file exists |
| A restore of a minutes-long ring blocks `stateLoad` for seconds | D8's accepted cost, as for a long sample; r8brain only when the rate differs; the README says so |
| The resize's main-thread copy never converges on a starved main thread | The bound `kAdoptCopyFrames` is 8,192 frames (170 ms at 48 kHz) of audio-thread copy; a main thread that can't copy 3 MB in 170 ms isn't running the editor either; the status shows "resizing" meanwhile |
| The tap's recursion runs away | Bounded by construction (§6.2); the `[slow]` worst-case test |
| Sub-project 3's filter or accessor changes under the rebase | The pyramid tasks are written after the rebase, against 3's merged code and its named taps |
| Position's `max` above 1 confuses a host that cached the old range | Nothing is frozen before M5; hosts rescan parameter info on load; the validators run with the new range |
| Content roots (5) and the captures folder (this) both extend the search | One `findSample` signature with both as arguments, agreed in the specs; 5 adds its `ContentPaths.h` alone under `media/` until this merges |

## 15. Left to the builder

- `RingStorage`'s layout for the levels (one allocation per level or one for all), and
  `LiveSource`'s cursor across a swap.
- The exact stable-region bound the main thread copies (how far below `R₀` it stops to leave the
  audio thread's bounded part), within `kAdoptCopyFrames`.
- The float32 WAV writer (the extensible header for stereo) and `decodeAudioBytes` over dr_wav's
  memory callbacks.
- How the `INST` and `AUDI` chunks are ordered after `PTCH` and `MEDI` (any order decodes).
- The status strings' exact wording for the cap and the resize.
- The `CommandEvent` struct's place in the plugin's editor queue (a kind beside the parameter
  change) and in `gg-render`'s merge.
- How `gg-render --write-media` renders the two state fixtures (a built-in spec each).
- Whether `engine-live-long`'s source positions are drawn by Spray alone or the case walks the
  ring; what matters is that the reads span the ring.
