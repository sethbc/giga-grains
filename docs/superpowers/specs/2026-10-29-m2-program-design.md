# M2 Giga: the program design

| | |
|---|---|
| **Status** | **Approved by the director 2026-10-29** (brainstorming; builder check and decisions 5 to 8 the same day; INTENT §23 #64). Not yet built: wave 1 starts with the program plan |
| **Milestone** | M2 (Giga), tagged 0.2.0 as an unpublished draft release at its close (INTENT §20, §23 #62) |
| **Depends on** | M1 closed (§23 #60); the installers and release pipeline merged (PR #13, §23 #61) |
| **Authority** | INTENT.md. This document places every M2 deliverable in a sub-project. Each sub-project then gets its own spec, plan, build and review, as M1's did (§23 #35). Part II changes below each add a dated §23 row |

## 1. Intent

M2 makes the engine "giga" (INTENT §5): four layers that mix and morph, minutes-long buffers that
survive with the session, thousands of grains kept safe by a deterministic budget, the quality tiers
and extreme-stretch tools, a modulation matrix, presets and content, and the rest of the UI. At its
close both products are the instrument INTENT describes, short of M3's voices, M4's sync and M5's
oscillators.

**Why a program design.** M2's deliverables are scattered: §20's M2 row names eleven, and twenty
more "until M2" promises sit in §14, §15, §17, §18, §19, §22 and the carry-over. The director asked
(2026-10-29) that everything be planned up front, so nothing is dropped and no sub-project discovers
late that another should have left room for it. This document is that plan: the inventory (§5), its
placement in seven sub-projects (§6), the decisions they all rely on (§7), and the exit criteria of
each and of the milestone (§6, §9).

**Director decisions** (2026-10-29, in brainstorming):

1. **One listening gate, at M2's close** (listening gate #2, §20). No intermediate packs for the
   director's ears. Each sub-project still renders its A/B files, for the machines and the record.
2. **Structure first.** The sub-project order below: layers and the parameter structure before
   persistence, before engine scale, before content and views. The alternative, leading with the
   long live buffer and persistence (§5's distinctive promise, the riskiest code), was declined: it
   would write the state format twice, once for one layer and again when the layers land.
3. **Everything is M2.** All three remaining §17 views (Edit, Mod, FX), not Edit alone with Mod
   and FX following in M3. By the same word, the two §22 items INTENT dates to M2 conditionally,
   the C++23 move and the AVX2 spike, are in as decided work (my reading of "everything"; §7.9,
   §7.10. The director may strike either).
4. **Agents run the sub-projects, in parallel where the dependencies allow.** The director offered
   (2026-10-29, while this document was being written) larger and more parallel sub-projects, "a
   series of agents working on projects and in turn using sub agents to do tasks". §4 and §6 take
   the offer: four waves, with up to three sub-projects running at once, and §12 describes the
   agent hierarchy. The director confirms the wave shape at this document's approval.

**Director decisions after the builder check** (2026-10-29, forced choices):

5. **The program manager approves each sub-project spec** against this document and INTENT; the
   director sees each spec's forced choices in one message and answers what they wish (§12).
6. **The live buffer's operations are M2's:** overdub, loop length, Morphagene-style splices and a
   record/capture gesture (INTENT §4, §5, §14, §17), which no milestone had dated. Sub-project 2
   builds the ring operations, 6 the gestures, 5 presets that use them (I38).
7. **The factory macro routes target every layer:** Macro Size moves Size on A to D together; the
   matrix still routes to one layer when a user asks (§7.3).
8. **The CC0 samples come from downloads with recorded provenance and from our own tools;** no
   recordings of the director's are planned (§7.8). The director creates the public content
   repository.

## 2. Goals, non-goals, anti-goals

**Goals.**
- Every item in §5's inventory built, or closed with a recorded reason, by M2's close.
- Both products render bit-identically to M1 with every new feature at rest: the 22 M1 goldens keep
  their WAV SHA-256s through the whole milestone, except where a commit says why one moved (§7.6).
- No CPU regression on the §16 headline case: `engine-sample`'s ratios against M1's baselines show
  no `regressed` verdict on any CI model at the close (§20's exit criterion, §23 #41).
- Deferred items are logged in `docs/m2-carry-over.md` and listed in each sub-project's final
  report (the carry-over count rules were retired by the director on 2026-10-30; §8 is history).
- Listening gate #2 passed by the director on the packs the sub-projects rendered.
- 0.2.0 tagged, its draft release made by CI and kept unpublished.

**Non-goals.**
- Anything public: no beta, no published nightly, no `GG_PUBLISH_NIGHTLY` (§23 #62).
- M3's voices, polyphony, MPE and per-voice envelopes; M4's sync scheduler; M5's oscillators and
  synthetic grains. The mod matrix's MIDI sources (§7.3) are monophonic channel messages; per-note
  modulation is M3's.
- The blocking CPU gate (1.0.0; rows 144, 148).
- Freezing the parameter IDs or identities: that happens at M5's first beta (§18). IDs are
  append-only regardless.

**Anti-goals** (a failure even if everything works).
- A feature that costs the audio thread an allocation, a lock or a CPU-adaptive branch.
- Output that depends on whether the host offered a thread pool (§7.4).
- A state that an M1 session can't load into, or an M2 session an older build can't skip past
  (§7.5).
- A benchmark headline quietly redefined to hide a regression.

## 3. Constraints

- CLAUDE.md's non-negotiables: audio-thread rules, determinism rules (counter RNG keyed by (seed,
  layer, stream, field, grain index); layers summed in a fixed order; block-size invariance),
  64-bit positions, the overload ladder's order, licensing.
- INTENT §8's principles break ties; §8.2 ("one engine, many faces") and §8.3 ("uniform source
  interface") shape sub-projects 1 and 2 in particular.
- Parameter IDs follow `(scope << 16) | index` with scope 0 global and 1 to 4 the layers (§18, §23
  #37), all below `0xB00000`. A new scope is a Part II change (§7.1).
- The state container stays version 1 with new chunks appended, unless a chunk's meaning changes
  (§7.5). Reading stays all-or-nothing; unknown chunks are skipped.
- Every dependency pinned to a SHA or tag and AGPLv3-compatible; the factory content is CC0 and
  stays out of this repository's history (§12; §7.8).
- The process is M1's: one branch and draft PR per sub-project; spec → plan → build → review;
  `scripts/check.sh` across `dev,asan,rtsan` and CI green before merge; goldens updated with
  `scripts/update-goldens.sh` and the WAV hashes reported; the carry-over counted at each
  sub-project's start and end.
- The gg-bench `reference` kernel is frozen; it is never edited to follow the engine, so every
  baseline stays valid.

## 4. Approaches

**The order.** Three were considered; the director chose the first.

- **Structure first (chosen).** Layers, the parameter scopes and the mod matrix land first, because
  every later sub-project writes into them: persistence saves per-layer media, the budget manager
  divides across layers, presets voice every layer and route, and the views draw them. Cost: the
  distinctive, riskiest code (minutes-long persistence) comes second, and the engine's extreme
  modes come third.
- *Persistence first.* The §5 promise and the deepest cut (state, media, loader) earliest. Rejected
  by the director: it does the state format twice.
- *Two parallel tracks from the start* (engine on one, UI on the other). Rejected: the mod matrix
  is the hub both tracks write to, so neither can start before it exists.

**The execution shape.** Two were considered once the order was set.

- *Strictly serial*, as M1 ran: one sub-project at a time, each merged before the next starts.
  Safe, slow: seven sub-projects end to end.
- **Waves (chosen, pending the director's confirmation).** The hub sub-project runs alone, then the
  three engine-side sub-projects run in parallel, then presets and views in parallel, then the
  close: four waves instead of seven steps. Each wave's sub-projects branch from the same `main`,
  own disjoint areas (§6, "Owns"), and merge in a fixed order, each rebased on the one before and
  re-verified. The cost is merge work in the shared files (`Processor`, `ParamIds.h`, the golden
  hash list), bounded by the ownership rules and the reservations §7.1 and §7.5 make in wave 1.

**The split.** Five sub-projects were proposed; the inventory made the fourth two sub-projects'
worth (presets and content; the views) and the engine's scale work two more (scale and quality;
extremes and the FX chain), so there are seven, in four waves. §6 justifies each boundary.

## 5. The inventory

Everything M2 owes, with where INTENT says so and the sub-project that owns it (§6). An item that
belongs to two is listed under the one that finishes it.

| # | Item | Source | Sub-project |
|---|---|---|---|
| I1 | Four layers A to D, each with its own source and settings, in ID scopes 1 to 4 | §5, §14, §18, §23 #37 | 1 |
| I2 | Mix and XY morph of the layers | §14, §17, §20 | 1 (engine); 6 (the pad) |
| I3 | Rendering split into tasks (layer × grain chunk, private accumulators, fixed-order sum); CLAP thread pool when offered, a loop otherwise | §15 (Threading), §22 | 1 |
| I4 | §22: which hosts besides Bitwig accept thread-pool requests under load | §22 | 1 |
| I5 | The full mod matrix: every §14 source (macros, LFOs, envelopes, random/S&H, step, MIDI, env follower, transport), user assignments, the macros re-targeted onto it, depths revisited | §14 (Macros), §17 (Mod view), §20 | 1 (matrix and sources); 6 (the view) |
| I6 | Pitch spread (per-grain pitch distribution) and the canvas's y axis freed from pan | §17 ("one pitch until M2's spread") | 1 (engine); 6 (canvas) |
| I7 | C++23 with a toolchain floor raise | §22, §23 #51 | 1 (first task; §7.9) |
| I8 | The resample/feedback source; the instrument's Feedback and its Macro 7 recipe | §4, §14 (Feedback), §17, §18, §20 | 2 |
| I9 | Carry-over 118, 119: the source-mode switch under the RT tripwire, and a fade instead of a hard cut | `docs/m2-carry-over.md` | 2 |
| I10 | Minutes-long live buffers; minutes-long samples; the 600 s cap replaced | §5, §14 (Loading, Live ring), §20 | 2 |
| I11 | Memory: a per-instance cap shown in the UI; a process-wide sample pool | §14 (Memory management), §21 | 2 (cap and pool); 6 (shown) |
| I12 | Persistence: `AUDI` (FLAC embed, `embed=1`), the 32 MB per-instance embed cap, the captures folder for larger ones, saving the live ring and captures; Freeze no longer "live until toggled" where the ring is restored | §18, §14 (Freeze; §23 #40), §20 | 2 |
| I13 | Collect and save | §18 (Missing media) | 2 |
| I14 | The budget manager: fixed weights per tier and channel count, the overload ladder (tier, drop, steal with fades), per-layer and per-voice quotas; carry-over 51 | §14 (Grain pool; Deterministic CPU budget), §16, §20 | 3 |
| I15 | The pool up to 16,384 grains | §14 | 3 |
| I16 | Mipmaps for upward transposition: offline for files, incremental halfband for live and resample buffers | §14, §19 | 3 (files, level selection); 2 (the incremental pyramids, after 3 merges) |
| I17 | The HQ tier (12- to 16-tap windowed sinc) and a Quality parameter giving the plugins Eco, Normal and HQ | §14 (Kernels), §23 #43 | 3 |
| I18 | The sweep harness: parameter sweeps, extremes and automation bursts with no NaN, crash, runaway gain or click | §19 (Fuzz and property tests) | 3 |
| I19 | §22: a timeboxed AVX2 (8-wide x86) spike, kept only if measured | §22, §23 #51 | 3 (§7.10) |
| I20 | Smear: coupled position jitter, overlap 8 to 32, micro-detune, power normalization | §14 (Extreme stretch), §20 | 4 |
| I21 | Spectral Smear: a paulstretch-style random-phase STFT source on pffft that grains read | §14, §15, §20 | 4 |
| I22 | Per-layer filter and grain buses with channel masks and per-bus FX; §22's "buses or per-grain filters" question answered | §14 (Units; Two-rate modulation), §17 (Filter panel), §22 | 4 |
| I23 | The global FX chain completed: delay and a real limiter beside the reverb | §14 (Mix and global FX), §17 (FX view) | 4 (my addition: §7.7) |
| I24 | CLAP preset discovery and `preset-load`; VST3 program lists and AU factory presets through clap-wrapper; the selector parameter at `0xC00000` noted | §18 (Presets; Parameter ID range) | 5 |
| I25 | Factory presets, MIDI-driven FX presets among them; the preset name in the header | §12, §20, §23 #32, §17 | 5 |
| I26 | The CC0 sample set (20 to 50 MB), its home outside git, the installers' reserved Content component, the plugins' content search path | §12, release-pipeline spec §6.9 | 5 (§7.8) |
| I27 | `gg-render` takes a preset | §19 | 5 |
| I28 | Mutate: randomize with per-parameter locks and an amount | §17, §20 | 5 |
| I29 | The preset browser, tags, favorites, INIT, undo/redo | §17 (Presets and editing) | 5 (browser, INIT); 6 (undo/redo) |
| I30 | The Perform view completed: one lane per layer, the layer strip, the XY pad | §17 | 6 |
| I31 | The Edit view: per-layer Source, Scheduler, Grain, Pitch, Space and Filter panels, each parameter with base, spread and distribution, and modulation depth | §17 | 6 |
| I32 | The Mod view: drag a source onto a control; depth rings | §17 | 6 |
| I33 | The FX view: the global chain; carry-over 217 (Reverb Size) decided there | §17, `docs/m2-carry-over.md` | 6 |
| I34 | The About box with the notices and licenses; carry-over 221 | `docs/m2-carry-over.md` | 6 |
| I35 | §17's basics: keyboard navigation, light and dark themes, color-blind-safe layer palette, resizing and HiDPI kept | §17 (Basics) | 6 |
| I36 | The timeboxed GPU-canvas spike (Visage or native Metal/GL) | §17, §23 #26 | 6 |
| I38 | The live buffer's operations: overdub (smoothed gains, crossfaded seams), loop length, splices, record/capture into an embedded or filed capture; the Perform gestures for freeze, record and drop | §4, §5, §14 (Live ring), §17 (Direct manipulation); decision 6 | 2 (the operations); 6 (the gestures); 5 (presets using them) |
| I37 | Listening gate #2; benchmarks against M1's baselines; the carry-over burn-down; INTENT §20 marked; the 0.2.0 draft | §20, §19, §23 #41 | 7 |

Items the inventory leaves where INTENT puts them, outside M2: carry-over 184 (M3, the Voice
scheduler); rows 144 and 148 (1.0.0); rows 85, 87, 88, 218 and 232 (upstream bumps; §8 says what
M2 does if a bump happens inside it).

## 6. The seven sub-projects, in four waves

Each goes through spec → plan → build → review on its own branch and draft PR. Each spec records
its carry-over count at start and end, names its goldens and benchmark cases, and renders its A/B
pack into `docs/listening/m2-<name>/` for the record, to be listened to at sub-project 7. Each
INTENT Part II change it makes adds a §23 row in the same change.

```
 wave 1        wave 2 (parallel)              wave 3 (parallel)        wave 4
 ──────        ─────────────────              ─────────────────        ──────
 1 Layers  ─► 3 Engine scale & quality  ─┐
   &       ─► 2 Sources & persistence   ─┼─► 5 Presets & content ─┐
 structure ─► 4 Extremes & FX chain     ─┘   6 Views             ─┴─► 7 Close
           ─► 5 and 6 start here too, and hold their wave-2-dependent tasks until wave 2 merges
```

**Wave rules.**
- A wave starts when every sub-project of the one before is merged and `main` is green, **except
  that sub-projects 5 and 6 start with wave 2** (the director, 2026-10-29: "parallelize as much as
  possible"): their specs and the work that depends on wave 1 alone (preset discovery, the preset
  container, the browser, the content pipeline, Mutate; the Perform view's lanes, strip and pad,
  the Mod view, the About box, themes, keyboard navigation, the GPU spike) proceed at once; the
  work that needs wave 2's parameters (voicing the presets; the Edit panels for wave-2
  parameters, the FX view, the memory readout) waits in their plans until wave 2 has merged,
  and they merge after it, rebased. The wave-1 worker likewise starts while the program's own
  PR is in CI, branched from that PR's commits.
- The sub-projects of a wave branch from that `main`. Each **owns** the directories and files its
  entry names and may edit the shared files (`core/include/gg/core/Processor.h` and its sources,
  `ParamIds.h`, `tests/CMakeLists.txt`, the golden hash list, INTENT) only within its own additions.
- **Merge order within a wave is fixed** (3, then 2, then 4; 5, then 6). Each later one rebases on
  the merged result, re-runs `scripts/check.sh`, the goldens and its benchmark cases, and re-states
  its hash claim before it merges. Conflicts are the later one's to resolve.
- One wave's sub-projects share no new parameter indices, chunk IDs or patch version (§7.1, §7.5).
- A sub-project that finds it needs another's unfinished work stops at the boundary and takes the
  dependency in the next wave rather than copying the other's code.

### 6.1 Sub-project 1: Layers and structure

**Delivers:** I1 to I7. Four layers in the engine and the `Processor`, each owning a source, a
scheduler and a grain stream, summed in a fixed order into the mix; XY morph as a modulation-free
mixer law in the engine (the pad comes in 6); per-layer parameters in scopes 2 to 4 mirroring scope
1's indices, and the new globals (morph X and Y, per-layer level and on/off) appended in scope 0;
the task-split renderer with CLAP `thread-pool` support, bit-identical with and without a pool; the
mod matrix with every §14 source and its determinism rules (§7.3), the eight macros re-targeted
onto it as fixed factory routings with their M1 depths; pitch spread as a per-grain field; the
remote-controls pages for layers B to D; `gg-render` specs addressing layers; the C++23 move as the
first task (§7.9).

**Owns:** `engine/` (Engine, the layer and task structure, the mod matrix and its sources), the
`Processor`'s layer and routing structure and the chain of §7.2, the source interface (read, plus a
mip-level accessor that returns one level until sub-project 3), the Source-kind parameter, the
patch migration, `ParamIds.h` and `ParamMap.h`, `tools/gg-render`'s spec, the toolchain (§7.9).
Runs alone: everything else depends on it.

**Headless by choice (my call).** The Perform view keeps showing layer A alone until sub-project 6;
layers B to D are reachable through the host's generic editor, automation and `gg-render`. M1's FX
slice took the same path (§23 #35), and the gate is at the close. The alternative, extending the
Perform view here, would put UI work in the engine sub-project and delay persistence.

**Exit:** both plugins expose four layers and pass every validator; a four-layer render is
bit-identical at every block size and with the pool on or off; the 22 M1 goldens' hashes are
unchanged with layers B to D off; new goldens cover a four-layer mix, a morph sweep, pitch spread
and each mod source; `gg-bench` gains `engine-layers` (4 × 500 grains) and `engine-layers-full`
(4 × 2,000), report-only beside X; `engine-sample` shows no regressed ratio; the Bitwig thread-pool
check is recorded (I4) and §22's row resolved (its in-host half at the close, below).

**INTENT:** §14 (layers, mod matrix sources and rules, pitch spread), §15 (threading as built),
§18 (the new IDs), §22 (thread pool, C++23 resolved), §23.

### 6.2 Sub-project 2: Sources and persistence

**Delivers:** I8 to I13 and I38's operations. The resample source: a per-layer tap on the instance's output (one block
late, §14) that a layer can granulate, giving the instrument Feedback and a writable buffer; the
source-mode switch made reachable, fading sounding grains instead of clearing the pool (rows 118,
119); the ring and sample caps replaced by the per-instance memory cap, with a process-wide sample
pool deduplicating files across instances; minutes-long rings and samples, with the ring-length
parameter (§7.2); the incremental halfband pyramids of the live and resample buffers, built after
sub-project 3 merges its file pyramid and level selection (it merges first); the `AUDI` chunk (FLAC
through libFLAC, `embed=1`), the 32 MB embed cap and the captures folder; the ring saved and
restored, which lets Freeze hold a restored ring (§23 #40 amended); `MEDI` version 2 with up to four
samples; collect and save; **the ring's operations** (decision 6): overdub with a smoothed gain and
crossfaded seams, a loop length within the ring, splices (marks the scan can address, saved with
the ring) and capture (the ring, or a splice of it, written as an embedded or filed capture a
Sample layer can then load), each deterministic on the control grid and declicked.

**Owns:** `engine/include/gg/source/` and its sources, `media/`, `SessionState.h`,
`SourceExchange.h`, `plugin/src/SampleLoader*` and `SearchCache*`, the `Processor`'s source
adoption and resample tap. Wave 2, merged second.

**Exit:** a session with a frozen minutes-long ring reopens and renders bit-identically on another
machine with its captures folder; a 16 s ring at Freeze renders bit-identically to M1 when nothing
is embedded; the memory cap refuses a load deterministically with a status; the RT tripwire covers
the mode switch both ways; libFLAC is pinned; new goldens cover resample feedback in the instrument,
a restored ring, and a four-sample instrument; `gg-render` loads `AUDI` from a spec.

**INTENT:** §14 (Loading, Memory management, Live ring, Feedback), §18 (`AUDI`, `MEDI` v2, captures,
collect and save, Freeze), §21, §23.

### 6.3 Sub-project 3: Engine scale and quality

**Delivers:** I14 to I19. The budget manager: a fixed weight table per tier and channel count, a
per-instance budget in weighted grain-samples per block, per-layer quotas, and the overload ladder
in §14's order with stealing by fade (row 51); the pool configurable to 16,384; the mipmap pyramid
for files (offline) and the kernels' level selection per grain from its rate, behind the mip-level
accessor sub-project 1's source interface already carries (one level until here); the HQ
windowed-sinc kernel, SIMD, bit-identical to its scalar reference; a per-layer Quality parameter
(Eco, Normal, HQ; default Normal) in both plugins, the ladder lowering below it only under
overload; the sweep harness as a test family; the AVX2 spike (§7.10).

**Owns:** `engine/include/gg/grain/` and its sources (pool, kernels, scheduler admission, tiers),
`Interpolate.h`, the mipmap code, `tools/gg-bench`'s cases, the sweep harness under `tests/`. Wave
2, merged first: the others rebase on the new pool.

**Exit:** overload at 16,384 one-second grains never clicks or drops out and degrades identically at
every block size; the M1 goldens' hashes are unchanged at Normal with no overload; `engine-sample`
shows no regressed ratio; `gg-bench` gains `engine-sample-hq` and an overload case, report-only;
the sweep harness runs in CI; the mipmap and sinc tests match their references bit for bit.

**INTENT:** §14 (budget, pool, kernels, mipmaps), §16 (figures beside X), §19, §22 (AVX2 resolved),
§23.

### 6.4 Sub-project 4: Extremes and the FX chain

**Delivers:** I20 to I23. Smear as one layer parameter coupling jitter, overlap, micro-detune and
power normalization; the Spectral Smear source (pffft STFT, random phases from the counter RNG) as
a source kind grains read from, instantiated for every tier against sub-project 3's references after
rebasing on it; the per-layer filter (sst-filters, on the 32-sample grid with
per-sample smoothing) and grain buses with channel masks, each bus with its own filter, answering
§22's question (§7.7); the global delay and a real limiter completing §14's chain, both at rest by
default so M1's output is unchanged.

**Owns:** the `Processor`'s wet chain after the grain sum (buses, filter, diffuser, delay, limiter,
mix), `core/src/space/` and the new FX units, the Spectral Smear source as a new source kind behind
sub-project 1's interface. Wave 2, merged third.

**Exit:** a 1000× stretch render at Smear 1 shows no comb peaks above a stated threshold where
Smear 0 does; the Spectral Smear source is deterministic across platforms by its event log and
within metric tolerances; filters and buses off, delay off and the limiter at its M1 law keep every
M1 golden hash; new goldens for Smear, Spectral Smear, a filtered bus and the delay.

**INTENT:** §14 (Extreme stretch, Units, Mix and global FX), §22 (buses resolved), §23.

### 6.5 Sub-project 5: Presets and content

**Delivers:** I24 to I29 (browser and INIT). The preset file format (§7.5) and CLAP preset discovery
and `preset-load`, with the VST3 and AU bridges checked and the identity tests extended to the
selector parameter; the factory presets, with a floor of 40 across INTENT §3's focuses and at least
8 MIDI-driven FX presets (note-freeze and note-repitch of the ring; §23 #32), each voiced with the
mod matrix and tagged; the CC0 sample set, its repository and release asset, the fetch script, the
installers' Content component filled, and the plugins' content search path (§7.8); `gg-render
--preset`; Mutate with locks and an amount, on the main thread; the browser (tags, favorites,
search, INIT) replacing the header's "Init".

**Owns:** the preset system in `plugin/` and `core/` (discovery, `preset-load`, the preset
container's reading), `scripts/fetch-content.sh` and the installers' Content component, the content
repository, the browser's UI files, Mutate. Wave 3, merged first.

**Exit:** every factory preset loads in all four formats in Live and Bitwig; each preset has a
golden render; the installers install the content and the plugins find it on all three platforms;
`gg-render --preset` renders a factory preset bit-identically to the plugin's state load; the
license set carries the content's CC0 notice.

**INTENT:** §12 (as built), §17, §18 (Presets, selector parameter), §19, §23.

### 6.6 Sub-project 6: Views

**Delivers:** I30 to I36, I29's undo/redo and I38's gestures. The Perform view completed (four
lanes, the layer strip, the XY pad, pitch spread on the canvas's y axis, the memory cap readout, and
the §17 gestures M1 left out: freeze, record and capture, drop a file onto a lane, splice marks); the Edit, Mod and
FX views on sst-jucegui; the About box with the notices (row 221); row 217 decided in the FX view;
undo/redo; keyboard navigation, light and dark themes, the color-blind-safe layer palette; the GPU
spike, timeboxed to one plan task, ending in `docs/spikes/m2-gpu-canvas.md` and a §23 decision
(the CPU renderer stays unless the spike beats it on the §16 UI budget with no new platform risk).

**Its spec may split it into two PRs** (Perform and Edit; Mod, FX, About and the spike) if the plan
exceeds M1's Perform UI in size, which it will. Both PRs belong to sub-project 6 for the carry-over
count.

**Owns:** `ui/` and `plugin/src/editor/` apart from the browser's files; the spike under
`docs/spikes/`. Wave 3, merged second. Its Edit panels take their parameter list from `ParamIds.h`
as wave 2 left it, so no view waits on an engine change.

**Exit:** every parameter is reachable from the UI; the canvas's frame time stays within §16's
budget with four lanes at 8,192 grains (`gg-bench --canvas` idle, and Live and Bitwig); the GUI
tests cover each view's open and close; the spike doc exists with its decision.

**INTENT:** §17 (as built), §16 (UI budget as measured), §23.

### 6.7 Sub-project 7: Close

**Delivers:** I37. Listening gate #2 on the packs of sub-projects 1 to 6 (one sitting, as decided);
an idle run on the reference core of every `gg-bench` mode, published in `docs/benchmarks/` beside
M1's, with X re-measured and compared; CI's report-only gate read for every model, any `regressed`
ratio explained or fixed; the carry-over burned down (§8); INTENT §20's M2 row marked closed and
§16's figures updated; `GG_VERSION_PRERELEASE` emptied and `docs/releases/v0.2.0.md` written; the
`v0.2.0` tag pushed and the draft release left unpublished, as `docs/releasing.md` says.

**Exit:** the director passes the gate; no regressed ratio stands; the carry-over count is at or
below its §8 target; `main` returns to `0.3.0-dev`.

## 7. Cross-cutting decisions

Mine unless marked. Each is a rule later sub-project specs build on; a spec that needs to break
one amends this document and INTENT together.

### 7.1 Parameter IDs

Layer parameters keep scope 1's indices in scopes 2 to 4: Position is `0x020001` in layer B, and
so on, so `slotForId` becomes arithmetic. Globals append in scope 0 from `0x000019`. **The mod
matrix's routings are not CLAP parameters:** they live in the patch (§7.5), so the matrix's size
can change before and after M5's freeze without burning IDs, and hosts automate the macros and the
parameters, never the routes. New modulators' own controls (an LFO's rate, say) are parameters,
per layer in its scope. **No new scope** is introduced without a §23 row; scopes 5 and up stay
reserved.

**Reservations for wave 2** (so three sub-projects can append parameters at once without colliding):
sub-project 1 ends with the next free index in each scope recorded in `ParamIds.h`, then reserves
`0x..40`–`0x..4F` for sub-project 2, `0x..50`–`0x..5F` for 3 and `0x..60`–`0x..7F` for 4, per
layer scope and in scope 0, as comments and a pinned test. A sub-project appends inside its block;
an index it leaves unused stays unused. Wave 3 adds no parameters.

### 7.2 Layers and the signal chain

Each layer owns its source, scheduler, stream and grain quota. **The chain, fixed in wave 1** (the
builder check asked for it; sub-projects 2 and 4 build onto it in parallel, so it can't be left to
them):

```
 per layer L (A..D):  Σ grains_L → buses/filter_L (4) → diffuser_L → × Level_L → softLimit → wet_L
                                                                                   └─► Feedback_L reads here (as M1)
 instance:            Σ_L morph(X,Y)_L · wet_L   (A..D in order; a layer that is off is skipped)
                      → delay (4) → [+ reverb return] → limiter (4) → mix with dry  → out
                                                                        └─► resample tap reads here
```

- M1's per-layer chain (Σ grains → diffuser → × Level → soft limiter) is kept per layer, Level and
  Feedback included, so one layer on at morph rest (A weighted 1, the others skipped, never added)
  renders bit-identically to M1; the global delay and limiter rest as the identity (§7.7).
- **The resample tap reads the instance output 256 samples late,** the quantum of M1's Feedback
  pieces, never the host block: block-size invariance is non-negotiable, and INTENT §14's "one
  block of latency" is amended to say 256 samples. The tap costs nothing, and adds no latency,
  while no layer's source is Resample.
- **Source kind is a per-layer stepped CLAP parameter, defined in wave 1** with its whole
  enumeration (Sample, Live, Resample, Spectral Smear; Oscillator reserved for M5), a value whose
  kind isn't built yet clamped to Sample or Live until its sub-project lands, so sub-projects 2
  and 4 add kinds without a patch change. **Both products expose every kind:** an FX layer may
  load a sample (the FX gets `MEDI` entries in 2 and a loader strip in 6), and an instrument layer
  may read live input (row 119's "live-input instrument"). My reading of §8.3 and "everything".
- The RNG key's `layer` field is the layer's index, as §14 always said. A layer whose source is
  Live owns its own ring fed from the instance's input, so per-layer Freeze stays independent;
  memory is the cap's business (§7.5).
- **The ring's length is a per-layer parameter** (seconds, default 16, in sub-project 2's block),
  bounded by the memory cap. Position keeps its seconds and its M1 values; its CLAP range widens
  to the longest ring and the engine clamps a Position past the ring's length to the ring. Nothing
  is frozen before M5, and the M1 goldens' Position values are unchanged.

### 7.3 The mod matrix

A fixed number of slots (the spec sets it; 32 is my expectation), each source → target with a
depth in the target's CLAP domain and a curve. Sources run on the 32-sample grid; every one is
deterministic: LFO phase from the absolute sample clock, random and S&H from the counter RNG keyed
(seed, layer, stream, `Field::Mod…`, step index), envelopes from the note gate, the env follower
from the input with fixed coefficients, transport from the host's position when it is given and
holding otherwise. The effective value stays `clamp(base + mod + Σ routes)` in one clamp. The
eight macros become factory routes with their M1 depths, so a resting preset is bit-identical;
revisiting the depths is the director's, at the gate. **A route's target is a parameter on one
layer or on every layer** (the matrix's target model). **The factory macro routes target every
layer** (decision 7), so a four-layer preset answers the macros as one instrument. **The instrument's Macro 7 becomes Feedback in wave 1,** not
in sub-project 2: the parameter and its recipe switch with sub-project 1's patch migration (an M1
instrument state's Macro 7 is folded into Stereo's base and reset to 0), and the route does
nothing audible until the resample source gives the instrument a writable buffer. One bump, one
owner (§7.5).

### 7.4 Threads

Rendering is a fixed list of tasks whose results are summed in a fixed order; a thread pool changes
only which thread runs a task, never a result. **The tasks are the layers, in M2:** one task per
active layer, each summing its own grains in spawn order into its private accumulator exactly as M1
does, so the M1 hashes hold. INTENT §15's "layer × grain chunk" is amended: chunking a layer's
grains re-associates the float sum and would move every M1 golden once, for a gain only a one-layer
instance in Bitwig would see; it goes to §22 as a later option with that cost named. The
determinism test renders with a stub pool of 1, 2 and 8 workers and with none and requires identical
output. No plugin-owned thread; the pool is the host's (`clap_host_thread_pool`), so VST3 and AU
hosts run the loop.

### 7.5 State

The container stays version 1. New chunks: `AUDI` as §18 reserves it; `MEDI` version 2 (up to
four `<sample>` entries, each with its layer) which an M1 build reads as "newer, no sample" by
design; the mod matrix and layer routing inside `PTCH` as PatchBase XML with a patch version bump
and a migration from version 1, so an M1 state loads with one layer and the factory macro routes.
The 64 MiB read cap and the 32 MB embed cap stand. **One owner per state change:** sub-project 1
alone bumps the patch version in M2 (for the routing structure); wave 2's sub-projects add
parameters the way M1's did, absent → default, with no bump; sub-project 2 alone adds chunks. **A
preset is a `GGST` container** with `PTCH`,
`MEDI` referencing factory samples by content-relative path, and no `AUDI`; the browser and
`gg-render --preset` read the same bytes a state load does. **`MEDI` version 2 carries a
content-relative reference** beside `path` and `relative`, so a factory preset names its sample by
its place in the content folder (§7.8) and sub-project 5 needs nothing from 2 after 2 merges. **The
`AUDI` codec is sub-project 2's choice** between FLAC at 24 bits (small, not bit-exact for the
ring's floats) and uncompressed float32 (exact, two to three times larger under the 32 MB cap), and
the program's criterion is worded for either: every machine that restores a session renders it
identically, and the restore matches the saved session's own render within the goldens' metric
tolerances; INTENT §18's "as FLAC" is amended if float32 wins. The memory cap is a setting, per
instance, saved with the state; its default and ceiling are sub-project 2's.

### 7.6 Goldens and benchmarks

Every M1 golden keeps its WAV SHA-256 throughout M2 unless a commit names the case and the reason;
`scripts/update-goldens.sh`'s hash list is the evidence, run before and after each DSP change. X
is unchanged in definition (one layer, 2,000 Normal grains, `engine-sample`); new cases are
report-only beside it until 1.0.0, each given baseline entries by `--make-baseline --merge` after
three full CI runs. A `regressed` verdict on `engine-sample` at any sub-project's end is fixed in
that sub-project, not carried.

### 7.7 The FX chain and buses (my addition, for the director to confirm)

§20's row doesn't name the global delay, the limiter or the layer filter, but §14 draws them, §17
gives them panels, and the director asked for everything: sub-project 4 builds them. The limiter
keeps M1's soft-limiter law as its default so no golden moves; its stronger mode is opt-in. §22's
buses question is answered in sub-project 4's spec by building two buses per layer with a filter
each and measuring whether per-grain filters are still wanted (my expectation: they wait for M5's
budget weights, as §14 says).

### 7.8 Content

A separate public repository, `giga-grains-content`, created by the director, holds the CC0 samples
and their provenance, and tags a release whose zip is the release asset INTENT §12 allows. **The
samples come from two sources** (decision 8): downloads under CC0 (Freesound's CC0 pool and the
like), each recorded with its URL, author, license and SHA-256 before it is admitted, and signals
our own tools generate (gg-render and scripts; textures, sweeps, noises that granulate well),
recorded as the command that made them. Nothing without a CC0 statement; no recordings are planned. CI fetches it at a pinned tag
and SHA-256 into the installers' Content component (release-pipeline spec §6.9); a local
`scripts/fetch-content.sh` does the same into `build/content`. The plugins search the installed
Content folder, then a user content folder in the settings file. Nothing from it enters this
repository's history. Not LFS: it costs money on GitHub and weighs every clone.

### 7.9 C++23

The first task of sub-project 1, so all M2 code uses it: `CMAKE_CXX_STANDARD 23`, the Linux leg on
gcc-13 or later (a toolchain floor raise, recorded in CLAUDE.md and §15), ClangCL and Apple clang as
they are, with the macOS 11.0 deployment target gating only the library features we then avoid. If
any leg can't be made green within the task's timebox, the task reverts and §22's row records why;
nothing else in M2 depends on it.

### 7.10 AVX2

A timeboxed task in sub-project 3, after the HQ kernel: an 8-wide x86 path chosen at load by CPU
features with the SSE path as fallback, bit-identical to it (as the SIMD kernels are to their scalar
references), kept only if the CI floor legs' in-process ratios improve beyond their spread, and
recorded in §23 with the ruling that a path chosen by CPU features with identical output is not
CPU-adaptive behaviour. Rebuilding the x86 baselines is part of keeping it.

### 7.11 Listening packs and host checks

Each sub-project renders its pack into `docs/listening/m2-<name>/` with a README and the specs that
made it, as M1's did, and no one listens until sub-project 7. The gate's checklist is one list
across all six packs. **Checks that need a DAW are the close's too:** the agents can't run Live or
Bitwig, so every "in Live and Bitwig" exit criterion above (the thread-pool survey, the presets in
four formats, the frame time) is met by a machine-checkable stand-in at the sub-project (the
validators, the headless plugin harness, `gg-bench --canvas`) and by the director's one sitting at
sub-project 7, whose checklist names each. A failure there is fixed in the close's fix wave.

## 8. The carry-over

> **Retired 2026-10-30** (the director): the count rules below no longer bind. Deferred items are
> handled as superpowers' subagent-driven development handles them and logged in
> `docs/m2-carry-over.md`. The placement table stays as a map of where each inherited row is expected
> to close.

13 open rows at M2's start. Placement:

| Rows | Where | Expected at the close |
|---|---|---|
| 51 | Sub-project 3 | Closed |
| 118, 119 | Sub-project 2 | Closed |
| 217, 221 | Sub-project 6 | Closed |
| 85, 87, 88, 218, 232 | Upstream bumps | Open unless a bump inside M2 fixes them. Sub-project 5 may bump clap-wrapper for preset discovery; if it does, it re-checks 87, 88 and 218 and runs the identity and AU state tests before accepting it |
| 144, 148 | 1.0.0 | Open |
| 184 | M3 | Open |

New rows number from 300. (The former target of 8 open rows and the no-net-growth rule are retired.)

## 9. Verification across the program

- Every sub-project: `scripts/check.sh` over `dev,asan,rtsan`, CI green on all legs via a draft
  PR, `scripts/update-goldens.sh` before and after with the hash list in the commit, the
  determinism tests, the validators on `release`, `ctest -L au` and the identity tests on any
  clap-wrapper change.
- New per-program tests: the thread-pool invariance test (§7.4), the overload ladder's block-size
  invariance, the state migration from an M1 state and a pre-container state, the preset round
  trip, the sweep harness.
- Benchmarks: new cases report-only; `engine-sample` watched for regression at every sub-project's
  end (§7.6). The close republishes every mode from an idle reference core.
- Listening gate #2 at the close, one sitting, six packs.

## 10. Risks

| Risk | Mitigation |
|---|---|
| Sub-project 1 grows into half of M2 (layers, matrix, threads, C++23) | Its spec may split the matrix's non-macro sources into a second PR; C++23 is a bounded task with a revert path |
| Four layers quadruple CPU and memory | The budget manager (3) and the memory cap (2); layers off cost nothing; X is one layer |
| The state format changes twice anyway (layers in 1, media in 2) | Both are additive: a chunk and a `MEDI` version, one patch migration, planned here so 1 leaves room |
| clap-wrapper's preset bridges or the selector parameter break identities or AU state | Identity and AU tests run on every bump; the bump is sub-project 5's and re-checks rows 87, 88, 218 |
| The CC0 set's provenance | Each sample's source and license recorded in the content repository; nothing without a CC0 statement |
| UI sub-project too large for one review | Two PRs under one sub-project (§6.6) |
| Three parallel engine sub-projects collide in `Processor` and `Engine` | Ownership per entry (§6), fixed merge order with rebase and re-verification, reserved ID blocks (§7.1), one state-change owner (§7.5); the program manager resolves what remains |
| Sub-project 2 grows (persistence, the memory cap, the ring's operations) | Its spec may split into two PRs under one sub-project, as §6.6 allows the views |
| A worker's spec drifts from this document or INTENT | The program manager approves each spec against both before the plan; the director sees its forced choices (§12) |
| The gate finds a sound problem six sub-projects deep | Every sub-project's pack exists from its own merge; fixes go through the close's fix wave, carried only if genuinely large |

## 11. Left to the sub-project specs

The slot count and curve set of the mod matrix; the morph law; the weight table and budget size; the
memory cap's default and ceiling; the `AUDI` codec (§7.5); mipmap level count and the halfband design; the sinc's taps and
window; Smear's coupling curves; the STFT size and hop; the filter models and bus count; the delay's
design; the preset file extension and tag vocabulary; the content folder layout; every view's
layout; the GPU spike's candidate order; each sub-project's golden and benchmark cases by name.

## 12. Execution: the agent hierarchy

Decision 4 of §1. Three tiers, each on the model tier the project's practice assigns (reviews and
decisions on the top tier; implementation on the tier below).

- **The program manager** is one session (this one's successor), driving the others as the
  `claude-session-driver` skill describes: it launches a worker session per sub-project when its
  wave opens, hands it this document and the sub-project's entry, reviews its spec, watches its
  progress, reviews its PR, resolves merge order, and keeps the carry-over count. It writes no
  product code.
- **A sub-project worker** is one Claude Code session per sub-project, in its own worktree
  (`scripts/worktree-setup.sh`), running the project's process end to end: brainstorm its spec from
  this document, write the plan, run subagent-driven development with a ledger, open the draft PR,
  run `scripts/check.sh` and the goldens, get the final review, fix every minor. It escalates
  behaviour and sound questions as forced choices and keeps engineering choices to itself.
- **Task subagents** implement and review single plan tasks, as in M1.

**Approval of the sub-project specs.** M1's specs were each approved by the director in
brainstorming. With seven specs, three at a time, that gate moves: the program manager approves a
spec against this document, and the director sees each spec's decision list (its forced choices) in
one message before the build starts, answering what they wish and letting the rest stand as the
worker's calls (decision 5).

**What the director still does:** the forced choices the workers raise; the listening gate; INTENT
Part I; publishing nothing.

## 13. INTENT changes at approval

With the director's approval of this document, in the same change:
- §20's M2 row gains the seven sub-projects and this document's path, as M1's row has its six.
- §14's "one block of latency" becomes 256 samples and the chain of §7.2 is drawn; §15's tasks
  become per-layer, with grain chunking added to §22 (§7.4).
- §22's thread-pool, buses, C++23 and AVX2 rows point at their sub-projects.
- §20's M2 row also gains I38 (decision 6), which no milestone had dated.
- §23 #64 records decisions 1 to 8 of §1, the wave shape, and the placement of §7.7, §7.8, §7.9
  and §7.10.
- INTENT §13 (How it's built) gains a line on the agent hierarchy, if the director wants it in Part I;
  otherwise §19's process notes take it (Part II).
