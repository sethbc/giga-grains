# M3 sub-project 1 (Voice slice): architecture brief

Read-only survey of the tree at `a3d5f2a` (branch `row-405-source-label`; the uncommitted
`docs/superpowers/specs/2026-11-07-m3-program-design.md` was read too, its §4 is the voice model this
brief serves). Paths are relative to `/home/sethbc/src/giga-grains`. "PH" = `core/include/gg/core/Processor.h`,
"PC" = `core/src/Processor.cpp`, "EH" = `engine/include/gg/engine/Engine.h`, "EC" = `engine/src/Engine.cpp`,
"CS" = `engine/include/gg/grain/CloudScheduler.h`, "GP" = `engine/include/gg/grain/GrainPool.h`.

---

## 1. The note path, end to end

### 1.1 Plugin (`plugin/src/GigaGrainsPlugin.{h,cpp}`)

- **Note port**: one input port, dialects `CLAP | MIDI | MIDI_MPE`, preferred CLAP (`GigaGrainsPlugin.cpp:2224-2237`).
  MPE is *advertised but not handled*: nothing reads per-channel bend/pressure/CC74 per note, and
  `CLAP_EVENT_NOTE_EXPRESSION` isn't converted at all (no hit for `NOTE_EXPRESSION` in `plugin/src`).
- **process()** (`GigaGrainsPlugin.cpp:460-560`): `countNoteOns` (460), `takeHandOffs` (467), the editor's
  queued values first (`takeUiChanges`, 473) and ring commands at offset 0 (475-479), then one pass over the
  host's events in host order (485-520), each sorted into `events_` (ParamEvent), `notes_` (NoteEvent) or
  `controls_` (ControlEvent). The first event that finds its array full stops the pass; the rest go through
  `applyBetweenBlocks` after the call (caps: `kMaxNoteEvents` 1024, `kMaxControlEvents` 1024,
  `GigaGrainsPlugin.h:222-229`). Transport is set before the call (523-538). One `processor_->process(...)`
  with the four spans (546-553).
- **params.flush while active** (`GigaGrainsPlugin.cpp:1264-1290`) and the overflow path both use
  `applyBetweenBlocks` (`:791-858`): values via `setBase` at once; modulation, notes and controls through a
  **0-frame `process()`** so they join the next grid boundary.
- **toNoteEvent** (`GigaGrainsPlugin.cpp:919-1011`, doc `GigaGrainsPlugin.h:543-556`):
  - CLAP `NOTE_ON` → `NoteEvent{On, channel, key, velocity}` (CLAP velocity 0..1 as is). If `note_id >= 0`
    and channel/key in range, the ID is remembered in `noteIds_` (256 slots, oldest replaced:
    `kMaxNoteIds`, `.h:230-232`; table `.h:785-795`; insertion `.cpp:932-948`).
  - CLAP `NOTE_OFF`/`NOTE_CHOKE` → Off. A known ID with channel=key=−1 resolves to that ID's (channel, key)
    (ruling R15, `.cpp:961-975`); `forgetNoteIds(channel,key)` drops matching IDs. **Release velocity is
    discarded** (NoteEvent has none) and **the note ID is not passed on** (NoteEvent has no ID field).
  - MIDI 0x9n (vel 0 → Off), 0x8n, and **CC 123 → `AllOff` on its channel** (`.cpp:984-1009`, ruling R14).
  - `NoteIdSlot`/`noteIds_` cleared by activate (`:290`) and reset (`:421`).
- **toControlEvent** (`GigaGrainsPlugin.cpp:888-917`, `isControlEvent` `:878-887`): CC 1 → ModWheel,
  0xDn → Aftertouch, 0xEn → PitchBend `(msb<<7|lsb)/16383`. Channel carried but "not otherwise read yet"
  (PH:104). **CC 64 (sustain), CC 74, poly aftertouch (0xAn) are not recognized** (fall through as no
  event). Adding CC 64 means extending `isNoteEvent` or `isControlEvent` (both stateless; whichever counts it
  toward which cap).
- **toParamEvent** (`.cpp:753-789`) ignores `note_id/key/channel` of param events: polyphonic modulation is
  treated as global today. No `CLAP_PARAM_IS_MODULATABLE_PER_*` flags anywhere.
- No `voice-info` extension, no `NOTE_END` output events.

### 1.2 Core events (PH:58-139)

- `NoteEvent {offset, type On|Off|AllOff, channel int16 (−1 any), key int16 (−1 any), velocity float}` (PH:68-87).
- `ControlEvent {offset, kind ModWheel|Aftertouch|PitchBend, channel, value 0..1}` (PH:91-106).
- Validation: `validNote` (PC:106-119), `validControl` (PC:123-127).
- For voices you will likely need: a note ID (int32), release velocity, and per-note expression events
  (a new span or new `ControlKind`s with a channel/note ID: Pressure, Timbre, PerNoteBend, Sustain).
  `process()`'s signature (PH:662-665) already takes four spans; a fifth, or widening ControlEvent, are both
  local changes; gg-render (Render.cpp:1111-1170) and `applyBetweenBlocks` mirror the call.

### 1.3 The control grid and event groups (Processor::process, PC:1592-1869; doc PH:328-350)

- `start = layers_[0]->engine.sampleClock()` (PC:1619): absolute clock.
- An event at a = start+offset joins boundary `b = max(ceil(a/32)*32, rendered)` (`join`, PC:1675-1694;
  `gridCeil` PC:92). A later boundary first applies the pending group (`applyPending`, PC:1629-1640):
  `render(up to b)` → `commitPending()` → `stepSources(b)` → `updateEffective()` → `syncFreeze()` →
  `retargetRamps(b)` → `applyCommands(b)`.
- Merge of the four spans by offset; ties: param, note, control, command (PC:1703-1795).
- Notes: `applyNote(n)` is applied **as the event is read** into `held_` (PC:1767), `hasPendingNotes_` set;
  only at `commitPending` (PC:669-706) does `updateKeys()` run (PC:700-704) and `midi_ = pendingMidi_` (705).
  So "only the held set after a group counts; a note that starts and ends inside one group does nothing"
  (PH:352-360). **Voices need a different rule**: an On+Off in one group must still be a (short) voice, or the
  spec must say it isn't. Also a voice needs the ordered list of the group's Ons/Offs (not the folded
  `held_`), so the group must queue note events (bounded array, like `pendingCommands_` PH:1172-1173).
- A boundary at or past the call's end carries over to the next call (PC:1797-1803). At most one pending
  boundary between calls.
- Time-varying routes make every cell an (empty) group (`applyCellsBelow`, PC:1646-1673; doc PH:318-326).

### 1.4 The held set, keys, gate, forced onsets

- `held_`: `std::array<uint16_t,128>`, one channel bit per key (PH:1179). On sets the bit; Off clears
  matching bits (−1 wildcards); AllOff clears a channel or all (`applyNote`, PC:777-815). Two notes on one
  (channel,key) share a bit → **carry-over row 184** (`docs/m2-carry-over.md:48`).
- `updateKeys()` (PC:817-831) builds `keys_` (distinct, ascending) and calls `engine.setHeldKeys` on **every
  layer** (829). `heldKeys()` accessor PH:680.
- Instrument: every engine has `setNoteGate(true)` (PC:289-292): no grain while no key held (CS:260).
- Forced onsets: `CloudScheduler::setHeldKeys` empty→non-empty sets `onsetForced_` (CS:225, CS:266-274);
  `Engine::setEnabled(true)` calls `cloud_.forceNextOnset()` (EC:431; CS:234). The pending onset moves to
  the block start = the boundary.
- `reset()` clears `held_` and calls `updateKeys()` (PC:961-962); MIDI sources reset (965-966).

### 1.5 FX notes-freeze (D4)

- `syncFreeze()` (PC:932-948): `noteHeld = Fx && keyCount_ > 0` (936); each layer's engine frozen when
  `(Freeze on && armed) || noteHeld`. Called at call start (PC:1625) and every applied boundary (1637).
  Doc PH:382-387. Program design §10 leaves "does D4 apply on FX Voice layers" to sub-project 1. If the held
  set becomes derived from voices, `keyCount_` must still mean "a key held" (pedal-held included or not:
  decide).

### 1.6 Velocity/key into ModSourceValues

- `pendingMidi_.velocity = unitClamped(n.velocity)`, `.key = key/127` on each On (PC:780-786): the **last
  note-on wins**, global (`MidiSources`, `core/include/gg/core/ModSources.h:164-167`).
- Controls write `pendingMidi_.{modWheel,aftertouch,pitchBend}` (PC:1777-1793).
- `midi_ = pendingMidi_` at commit (PC:705). `sourceValue()` reads `midi_` (PC:616-653).
- Env 1/Env 2 gate = `keyCount_ > 0` at each stepped boundary (`stepSourcesAt`, PC:734-764, gate at 737).

---

## 2. The engine side

### 2.1 One stream per engine today

- Engine = one layer: one `GrainPool pool_` (EH:532), one `CloudScheduler cloud_` (EH:533), one `LiveSource
  live_` (EH:534), optional SpectralSource. Class doc EH:82-186.
- `kStream = 0` with the comment **"A layer has one stream (spec §6.2): stream 0. M3's Voice scheduler adds
  streams."** (EC:48-49).
- `processBlock` (EC:478-586): fill outputs; write ring (509-512); Spectral smear with
  `cloud_.params().position` (522-544: **one Position per engine**); `spawn` lambda (550-576) turns each
  `GrainDraw` into a `GrainSpec` (`sampleGrain` EC:588-618 / `liveGrain` EC:620-670) and calls `admit`;
  `cloud_.schedule(blockStart, n, seed, layer, kStream, spawn)` only if `enabled_` (577-580);
  `renderPool(...)` (581), `retireFinished`, `retirePrevious`.
- `Engine::process` (EC:413-476) applies requests at its first frame: source mode, enable gate (on → forced
  onset; off → `clearGrains`, `bus2Used_=false`), freeze, `quota_ = requestedQuota_` (440), ring limit; then
  splits into `maxBlockSize` blocks.

### 2.2 CloudScheduler (CS:121-470)

- Recurrence t(k+1) = t(k) + max(P((1−d)+dE(k)), 1/16) (CS:124-133); `schedule()` template (CS:254-412).
- Per grain at onset k: keys `rng::hash({seed, layer, stream, field, k})` (CS:291-293); Note draw only if
  `keyCount_ > 0` (CS:297-305); pitch = Pitch + (key−60) [+ spread, smear detune] clamped ±48 → `exp2Det`
  (CS:306-329); duration (+size spread) (330-340); position by mode (341-378); bus (380-383); pan (384-388);
  Onset draw (404); `emit(g)` (410).
- **Everything a grain gets is latched at its onset** (`GrainDraw`, CS:79-119 → `GrainSpec`, GP:29-62): rate,
  start position, duration, texture, gain, pan, tier, bus, level, source slot. Within one engine's stream
  the only per-grain variation is the RNG draws and the key pick; the params (`params_`) are one set per
  scheduler. **Nothing varies within a grain's life** (no rate or gain ramp): kernels compute
  `pos = startPos + rate·e` and multiply latched `gainL/gainR` (`engine/include/gg/grain/GrainKernels.h:72`,
  683-699). Per-sample pitch following (a slide inside a grain) would be a new kernel feature; per-grain
  following is free (a voice's scheduler gets new `pitchSemitones` per cell).
- Density/distribution change rescales the pending onset (ruling M1-h, CS:135-143, 276-285); `setParams`
  same values twice = once (Processor exploits this, `sameCloud` PC:177-197; `static_assert(sizeof(CloudParams)
  == 120)` PC:177 — adding a CloudParams field requires updating `sameCloud`).
- A voice can reuse `CloudScheduler` unchanged: give it **no held keys** (so no Note draw, `rate =
  pitchRatio_`), fold `key − 60 + bend` into `CloudParams::pitchSemitones`, no note gate, `forceNextOnset()` at
  the voice's start, stop calling `schedule()` when the voice ends. Its `stream` argument is the natural slot
  for the voice key (below).

### 2.3 RNG key (`engine/include/gg/core/Rng.h:29-66`)

- `Key {uint64 seed; uint32 layer; uint32 stream; Field field; uint64 index}`, hashed member by member with
  splitmix64 (Rng.h:58-66). Fields are append-only (Rng.h:27-44; next free is 13).
- Users: CloudScheduler (layer, stream=0); mod Random sources `{seed, layer, 0, ModRandom1|2, k}`
  (`core/src/ModSources.cpp:161`); SpectralSource `{seed, layer, channel c, SpectralPhase, …}`
  (`engine/src/SpectralSource.cpp:312`); Mutate `layer = stream = 0xFFFFFFFF/0xFFFFFFFE`
  (`core/include/gg/core/Mutate.h:33-36`); gg-render noise (`tools/gg-render/Signals.cpp:83`).
- The program design writes the voice key as a sixth member "(seed, layer, stream, field, voice ordinal,
  grain index)" (`2026-11-07-m3-program-design.md` §4.5). **Adding a member to `hash()` changes every
  existing draw** unless it is mixed in only for voices. The zero-risk encoding is **stream = 1 + ordinal**
  (uint32; Cloud keeps stream 0, so every Cloud draw is untouched; collisions with Spectral's stream 0/1 are
  impossible because the fields differ). The spec should say which, and pin a literal hash in
  `tests/engine/rng_tests.cpp` and `cloud_tests.cpp:578` ("each draw has its own key, with the real layer
  and stream").

### 2.4 Pool, ledger, quota, ladder, steal fade, gate (per engine)

- Pool: SoA, storage 2× capacity, spawn order, stable compaction (GP:113-139). `soundingWeight(onset)`,
  `fadingWeight`, `soundingCount`, `oldestSounding`, `steal` (GP:252-292), `spawn(g, edge)` (GP:308).
  **Requirement: onsets passed to them must not decrease between clears** (GP doc at ~245-251). The cursor
  in `oldestSounding` relies on it too.
- `admit()` (EC:672-777): storage-full early retire (676-692); ladder: fits at tier → lower tier → steal
  oldest unstolen grain past `kStealThreshold`=0.5 of its window if its weight makes room (EC:693-745;
  constants `engine/include/gg/grain/Budget.h:22-54`: weights, `kStealFade`=256, `kEdgeFade`=256,
  `kStealThreshold`, `kMaxGrainCapacity`=16384); steal reported to `stealSink_` (736); spawn; spawn sink
  (775).
- Quota: per engine (`setQuota`, EH:321-326, applied EC:440). The Processor splits Budget equally among
  rendering layers each piece (`budgetUnits_ / renderingCount`, PC:2119-2137; doc PH:276-283). Voices on a
  layer would share that layer's quota automatically if they share the engine's pool (program design §4.5:
  the split among voices is sub-project 2's).
- Steal fade: `GrainPool::steal(row, onset)` ends a grain at onset+256 with a raised cosine (EH:160-179,
  GP:192-203 doc). A voice steal (16-voice cap) can reuse it per row, but `steal` asserts "not stolen
  before" and "sounds at the onset", and the ladder's `stolen_`/steal sink would count those steals unless a
  separate path is added. To steal "a voice's grains" the pool needs a per-row voice id (a new SoA array) or
  the voice must track its rows (rows move on compaction, so a per-row tag is simpler).
- Enable gate: `setEnabled` (EH:295-304) — off clears the pool, on forces an onset; the Processor flips it
  only at its ramp's end or on grid (PC:1876-1914).

### 2.5 Where "N streams per engine" fits with least disturbance

Recommended shape, keeping Cloud's code path byte-for-byte:

1. Keep `cloud_` and the existing `spawn`/`schedule` call for Cloud mode. Add to Engine a fixed array of up
   to 16 voice schedulers (`CloudScheduler` + voice state: ordinal, alive/releasing, its CloudParams), set
   between process() calls like `setCloudParams` (threading rule EH:264-270).
2. In `processBlock`, when the layer is in Voice mode, replace the single `cloud_.schedule(...)` (EC:579) by
   a **k-way merge by onset** of the live voices' schedulers (ties broken by voice ordinal), feeding the
   *same* `spawn` → `admit` lambda. The merge is mandatory: admission and the ledger need non-decreasing
   onsets (GP requirement above), and spawn order decides summation order. `CloudScheduler::schedule`
   emits a whole block at once, so a merge needs either a "peek next onset / emit one" API on the
   scheduler or a bounded per-voice staging buffer (bound: ~16 grains/frame/stream at d=1 (CS:182-189) ×
   block length — large; the peek API is cheaper and allocation-free).
3. Share the one pool: one ledger, one quota, one capacity (4096 by default, PC config
   `ProcessorConfig::grainCapacity` PH:53-55; INTENT §14 notes M3 may raise it), one bus split, one
   renderPool. Grains of all voices are then summed in **spawn order**, interleaved across voices. That is
   deterministic and block-size invariant, but it is *not* literally "voices summed in start order". If the
   spec means per-voice sums in ordinal order (or a per-voice amplitude envelope applied per sample), the
   engine must render each voice into its own buffer (voice-major), which needs per-voice scratch
   (16 × 2 × pieceMax floats) and changes how `renderPool` partitions rows — a bigger change. Decide in the
   spec: (a) per-grain latched voice gain (envelope sampled at each grain's onset, like `gain` today), summed
   in spawn order; or (b) per-sample voice envelope with per-voice buffers summed by ordinal.
4. Spectral kind: the smear reads one Position (`cloud_.params().position`, EC:542). On a Voice layer with
   Spectral kind, voices can't each move the smear's read point; spec should restrict or define it.
5. Engine-level things that stay per layer (not per voice): ring write and freeze, Spectral smear, bus
   filters, diffuser, Level ramp, soft limiter, Feedback (all in `renderLayerPiece`, PC:1871-2063).

### 2.6 How the event log identifies streams

- Sinks: `GrainEventSink(ctx, layer, const GrainSpec&)`, `GrainStealSink(ctx, layer, index, onset)`
  (EH:71-80). The Processor buffers per layer (`LayerEvent`, PH:931-941; `onGrain`/`onSteal` PC:1389-1421)
  and drains in layer order (`drainEvents`, PC:1423-1453); also feeds the UI's `GrainQueue` (GrainVis has a
  layer, no voice).
- gg-render's line: `{"i","layer","onset","frac","pos","rate","dur","tex","tier","lvl","gain","pan"}`
  (`tools/gg-render/Render.cpp:1190-1196`), steal line `{"i","layer","steal"}` (`:1198-1201`). **No stream
  field.** `i` is the scheduler's k, so voices restarting k at 0 would give duplicate `i` per layer.
- To identify voices without moving any Cloud line: add a `voice` member to `GrainSpec` (GP:29-62; it's
  `operator== = default`, only tests compare) and emit `"v":<ordinal>` (or `"voice"`) **only** when the
  grain is a voice grain, after `"layer"`. The steal line needs the same, and a voice steal (16-cap) needs
  its own line kind if it isn't the ladder's. Lines are compared exactly per line (CLAUDE.md "Golden
  renders"), so Cloud lines must keep their exact text.

---

## 3. The mod matrix

- `ModSource` enum (`core/include/gg/core/ModMatrix.h:28-56`): the index into `ModSourceValues`
  (ModMatrix.h:175-178), saved by **name** (`modSourceName`, `kSourceNames` in `core/src/ModMatrix.cpp`), so new
  sources append before `Count` freely (patch format by name, PatchState.h:43-50). gg-render's route
  `source` is the name too (RenderSpec.h:264-290).
- Per-layer sources: `Lfo1..Step` (`isPerLayerSource`, ModMatrix.h:99-102); time-varying: those + Env
  Follower + Transport Beat/Bar (ModMatrix.h:106-110).
- `applyRoutes(routes, sources[4], offsets)` (`core/src/ModMatrix.cpp:~58-95`): slot order; layer targets
  fan to `targetLayer` or all four; per-layer sources read the sourceLayer or the target's layer; offsets
  `+=` per slot. 48 slots (`kModSlots`).
- Factory routes: slots 0-11 = M1's macro recipes bit for bit (`factoryRoutes`, ModMatrix.cpp:97-118); they
  are INIT's (`initState`, `core/include/gg/core/Preset.h:60-62`) and every packed factory preset's base.
- **Stepping** (PH:304-316): `stepSourcesAt(c)` (PC:734-764) once per grid boundary in order, before the
  effective values at c: per layer LFO×2, Env×2 (gate = held set non-empty, PC:737), Random×2 (keyed
  `{seed, L, 0, ModRandom1/2, step}`), Step; then follower and transport. `stepSources` loops every boundary
  since the last (PC:766-775) — called from `render()` (PC:2077) and `applyPending` (PC:1636). Modulators are
  stepped on the audio thread between pieces, never in a task (PH:1018-1022).
- **Application** (PH:290-302; `updateEffective`, PC:833-893): macros' effective first; `sources_[L].v[i] =
  sourceValue(i, L)` for all 4 layers (PC:847-853); `applyRoutes` (857); every slot
  `clamp(base+mod+offset)` (858-862); per layer `sendCloudParams(L)` only when `CloudParams` changed bit for
  bit (PC:900-916), source kind, ring loop/splice; budget, space, delay, limiter.
- So targets are applied **per applied boundary**, not per sample. Ramped targets (Mix, Amount, Dry to Reverb,
  Morph X/Y, per-layer Level, Diffusion, Feedback, On, Overdub, Resample) get a 10 ms linear ramp from each
  boundary (`retargetRamps`, PC:357-383; PH:407-411). Engine targets reach the grains only at onsets
  (latched). Bus filters re-read effective values at every grid cell inside the piece loop
  (`busParams`, PC:2022-2036).
- With no time-varying route, only boundaries with events are applied; the sources still step every cell
  (PH:318-326). `timeVarying_ = anyTimeVarying(routes_)` (PC:162-175), recomputed by `setRoutes`
  (PC:599-605).
- **Env 1/Env 2 today**: one `Adsr` pair per layer (`LayerModulators`, ModSources.h:202-209), gated by the
  instance's held set. `Adsr` (ModSources.h:66-89; step `core/src/ModSources.cpp:96-146`) has no public
  "idle" query; a voice that ends when its envelope finishes needs one (stage_ is private). Env time range
  is ~1 ms to 10 s (`envelopeTime`, `core/src/ParamIds.cpp:84-89`; defaults :244-259).
- **Per-voice evaluation, what it takes**:
  - Per voice: its own `Adsr env[2]` (gated by the voice's note and the pedal), its velocity, key, note
    pitch, pressure, timbre, release velocity; a `ModSourceValues` for the voice = the layer's array with the
    per-voice entries replaced.
  - A per-layer variant of `applyRoutes` (only routes reaching that layer's targets; global targets aren't
    per voice) producing the voice's offsets for the layer's 62 slots, then the voice's `toCloudParams`
    (`core/include/gg/core/ParamMap.h:111-139`) from base+mod+voice offsets. `toCloudParams` reads by slot
    from a full `ParamValues`, so either a 283-float scratch per voice (16×4×283×4 B ≈ 72 KB, fine,
    preallocated) or a layer-slice variant.
  - Cost per applied boundary per voice: 48 routes + 62 clamps + one `toCloudParams` (a few `exp2Det`) +
    `sameCloud`. At 64 voices and every cell (Env routes are time-varying), roughly 64 × ~300 flops / 32
    samples ≈ 600 flops/sample — small beside the grain kernels, but it runs on the calling thread
    serially (stepping happens outside the layer tasks), so it is pure serial cost.
  - Per-voice Level/velocity-to-Level: Level is a per-layer ramp applied after the engine
    (`renderLayerPiece`, PC:2016-2059). Per-voice level must go into the grain gain (latched at onset) or
    per-voice buffers (§2.5 point 3).
  - New `ModSource` values that are per voice need a third class beside per-layer/global in `applyRoutes`'
    `from()` logic and in `isTimeVarying` (Env per voice is time-varying; Pressure/Timbre are event-driven).

---

## 4. Parameters and state

- **Layer params**: X-macro rows `X(Name, index, "Display")` in `core/include/gg/core/ParamIds.h:56-130`
  (`GG_LAYER_PARAMS_M1`, `_M2`, `_M2_SOURCES` 0x40-0x44, `_M2_ENGINE` 0x50, `_M2_EXTREMES` 0x60-0x67;
  the rules for appending at ParamIds.h:39-55). ID = `(layer+1)<<16 | index` (ParamIds.h:42, 474-480).
  Free layer indices: 0x31-0x3F (`kNextFreeLayerIndex = 0x31`, ParamIds.h:349, pinned by
  `tests/core/patch_tests.cpp:430`), and the unused tails of the reserved blocks (0x45-0x4F, 0x51-0x5F,
  0x68-0x7F). Scope indices stop at 0x7F (`kScopeIndices = 0x80`, ParamIds.h:156); going above needs that
  table widened. A new list (e.g. `GG_LAYER_PARAMS_M3(X) X(Mode, 0x31, "Mode")`) is added to both
  `GG_LAYER_PARAMS` and `GG_LAYER_PARAMS_SINCE_M1` (ParamIds.h:122-130), index column strictly ascending
  (`validIndexColumn`, ParamIds.h:167-197) — a 0x31 list goes between `_M2` and `_M2_SOURCES`.
- **Slots** (ParamIds.h:218-298): layer A's new rows follow the 28 M1 slots; then B, C, D in full; then the
  globals (MorphX … LimiterRelease). A new layer row **shifts every later slot** (never an ID).
  `kLayerParamCount = 62` (ParamIds.h:151) → 63; `kParamCount` 283 → 287 (+1 per global).
- **Global params**: append to `ParamSlot` after `LimiterRelease` (ParamIds.h:296) and to `kParamIds`
  (ParamIds.h:418-483), scope 0; next free global index 0x22 (`kNextFreeGlobalIndex`, ParamIds.h:349).
- Metadata: `paramMeta` tables built in `core/src/ParamIds.cpp` (`buildTable`, ~:520; layer rows by
  `LayerParam` switch, e.g. :244-259). Stepped params get `CLAP_PARAM_IS_STEPPED`. gg-render names come from
  the same macro (`tools/gg-render/RenderSpec.cpp` expands it), so "A Mode" … "D Mode" appear automatically.
- Places that must follow a new param: `tests/support/ParamTable.h:202` (the parallel table),
  `tests/core/patch_tests.cpp:302-303,505` (count 283, 62), `tests/plugin/identity_tests.cpp:31`,
  `tests/plugin/params_tests.cpp:306`, `tests/plugin/rt_tests.cpp:298`, `ui/include/gg/ui/EditLayout.h`
  + `tests/ui/edit_layout_tests.cpp:47` ("every parameter has exactly one placement"), the editor frame test
  "every parameter is reachable from the UI", Mutate's structural locks (`core/include/gg/core/Mutate.h:38-45`;
  Mode is structure and should be locked), remote-controls pages (`GigaGrainsPlugin.h:504-507`),
  `isValidTarget` (ModMatrix.h:135-148) if Mode must not be a route target.
- **Patch** (`core/include/gg/core/PatchState.h`): `kPatchVersion = 2` (:78); params by CLAP ID; an absent
  param takes its default (:72-74, :99-100); routes in `<mod>` by name. Migration hooks in
  `core/src/Patch.cpp:396-475` (`migratePatchFromVersion` :467; version-1 → factory routes) and
  `core/src/Patch.h:57-124`. A Mode param defaulting to Cloud needs **no** version bump; new *default
  routes* do, if old (v2) states/presets should gain them on load (a v2 state's `<mod>` holds exactly its
  saved routes).
- **Presets** (`core/include/gg/core/Preset.h:21-62`): GGST container with PTCH (+`<preset>`) and MEDI;
  `keepRingParams` / `keepRingLengths`; `initState(mode)` = defaults + `factoryRoutes(mode)`, seed 0.
  Factory presets are packed at build time from `content/presets/` sources by `gg-render --pack-preset`, "the
  product's defaults and factory routes under the source's" — so **changing `factoryRoutes` changes every
  packed factory preset's routes**.
- **Mutate** (`core/src/Mutate.cpp:109-160`, key :74-75): each slot's draw is keyed by `counter × kParamCount
  + slot` (slot index!). Appending any parameter changes `kParamCount` and shifts slots, so **every Mutate
  press on every preset moves** (`gg-render --mutate-preset`, the listening pack's ladder). The sweep harness
  already re-keyed by CLAP ID for this reason (`tests/sweep/sweep_keying_tests.cpp`). Not an audio golden, but
  a behavior change the spec should name (or re-key Mutate by ID first, as its own commit).

---

## 5. gg-render

- Spec doc `tools/gg-render/RenderSpec.h:30-150`; every unknown key is an error.
- Notes: `NoteSpec {time, key, duration, channel?, velocity?}` (RenderSpec.h:234-256), parsed by `readNotes`
  with allowed keys `{"time","key","duration","channel","velocity"}` (`RenderSpec.cpp:250-292`, key list :261);
  time order required; R17 (Off must land after On's frame). Converted by `timedNotes`
  (`Render.cpp:262-295`): On at round(t·sr), Off at round((t+dur)·sr) unless past the end; Offs before Ons
  on one frame.
- Controls: `ControlSpec {time, kind modWheel|aftertouch|pitchBend, value 0..1, channel?}`
  (RenderSpec.h:292-306), `readControls` (`RenderSpec.cpp:440-478`, keys :449, kind check :463-466),
  `timedControls` (`Render.cpp:388-415`).
- Call loop (`Render.cpp:1100-1170`): per block, slices of automation, notes, controls, commands at offsets;
  one `processor.process(...)`; `takeEventLines`. Sinks set at `Render.cpp:1052-1053`.
- **Adding MPE/CC64**: extend `ControlSpec.kind` with `"sustain"` (CC 64, value 0/1 or ≥0.5) and per-note
  kinds `"pressure"`, `"timbre"`, `"noteBend"` that address a note (by `channel` in MPE style, or a new
  `note` key naming the NoteSpec's index / an `id`), or add per-note curves inside `NoteSpec` (e.g.
  `"expression":[{"time","kind","value"}]`, relative to the note), plus `"releaseVelocity"` on NoteSpec. Each
  needs its key in the `Object(...)` allow-list, its validation, and a mapping in `timedNotes`/
  `timedControls`. Bend range fixed ±48 in sub-project 1 (program design §8 row 1).
- **Adding goldens**: drop `tests/golden/cases/<name>.json`; `tests/CMakeLists.txt:618-632` globs them
  (CONFIGURE_DEPENDS) into `golden_<case>`; run `scripts/update-goldens.sh` to write
  `tests/golden/expected/<case>.{metrics.json,events.jsonl}`; each new case must catch all 16 injected
  steps or be added with a reason to the table in `tests/metrics/click_injection_tests.cpp` (~:168;
  cases at :268-278). Look at the spectrograms; commit message names why.

---

## 6. Tests that pin Cloud bit for bit, and where voice tests go

**Must not move (the at-rest gate):**
- The 60 goldens `tests/golden/cases/*.json` + 48 preset goldens `tests/golden/presets/cases/*.json`;
  `scripts/update-goldens.sh` before/after prints every WAV SHA-256 and lists changed cases (CLAUDE.md).
  Note-driven ones: every `inst-*` (21 cases), `fx-note-freeze`, `fx-mod-*` with notes, `inst-mod-midi`
  (velocity/key/bend/aftertouch sources), `inst-mod-env` (Env gating by held set), all 24 instrument presets
  and the 8 `midi` FX presets.
- Engine determinism: `tests/engine/determinism_tests.cpp` (133, 161, 265, 435, 733, 1181/1186 overload,
  1283 spectral); `tests/engine/cloud_tests.cpp` (578 key layout, 884, 955, 1013, 1063 forced onset, 1117
  note gate); `tests/engine/rng_tests.cpp`; `engine_reference_tests.cpp`.
- Core: `tests/core/note_tests.cpp` (354 note-on starts cloud, 373 note inside one cell does nothing, 388,
  416 chord, 458 held set/wildcards/AllOff, 502 reset, 528-650 FX freeze, 733/810 block-size invariance, 857
  no alloc); `tests/core/mod_processor_tests.cpp` (249 factory routes = macros, 319 envelope follows held
  set, 447 MIDI sources on the grid, 782 modulation block-size invariant, 848 reset == fresh, 932
  time-varying split); `tests/core/processor_tests.cpp` (187, 562, 779 every slot automated, 872, 1764 resting
  preset bit-identical); `tests/core/layers_tests.cpp` (1016 event order, 1120 four layers invariant, 1491
  setBases); `tests/core/threads_tests.cpp` (115, 241, 513: same bytes on any pool);
  `tests/core/mod_matrix_tests.cpp` (factory routes = M1 `macroOffsets` copy).
- Plugin: `tests/plugin/process_tests.cpp` (337-398 note counting, 587 FX freeze, 684 MIDI notes, 755 off by
  note id, 815, 853 notes block-size invariant, 1176 controls+velocity); `tests/plugin/rt_tests.cpp`
  (tripwire on process/flush).
- Sweep: `tests/sweep/*` iterate all slots (`SweepDriver.cpp:64`, `sweep_extremes_tests.cpp:37,145,174`);
  draws keyed by CLAP ID, but a new param's ID enters the pick lists, so some sweep case may begin sweeping
  Mode and render Voice — re-check the floors.

**New voice tests would live in:**
- `tests/engine/voice_tests.cpp` (new; tags `[engine][voice]`, add to `tests/CMakeLists.txt` sources): the
  merged schedule's onset order, per-voice key (stream = 1 + ordinal), shared ledger, voice steal fade,
  release continues, block-size invariance (also add cases to `determinism_tests.cpp`).
- `tests/core/voice_processor_tests.cpp` (new) or extend `note_tests.cpp`: note identity by ID / (ch,key,count)
  (closes row 184), 16-cap steal, pedal, per-voice Env, per-voice sources, Cloud layers unchanged beside a
  Voice layer, held set derived from voices, reset, `[rt]` no allocation, `[determinism]` block sizes,
  `[threads]` any pool (extend `threads_tests.cpp`).
- `tests/core/mod_processor_tests.cpp` for per-voice route evaluation; `mod_matrix_tests.cpp` for new
  sources' names.
- `tests/plugin/process_tests.cpp` for CLAP note IDs → voices, MPE channel bend/pressure/CC74, CC 64.
- `tests/tools/render_spec_tests.cpp` for new spec keys and their errors; new `tests/golden/cases/inst-voice-*.json`
  / `fx-voice-*.json`.
- `tests/core/patch_tests.cpp` (counts, IDs, defaults; migration if the version bumps), `tests/support/ParamTable.h`.

---

## 7. Risks: where voices could silently move Cloud's bits or break block-size invariance

1. **RNG key**: adding a `voice` member to `rng::Key`/`hash` changes every draw in the product. Encode the
   voice in `stream` (Cloud stays 0) or mix it in only on the voice path.
2. **Default voice routes in `factoryRoutes`**: they become INIT's and every packed preset's routes. A route
   from an *existing* source (Velocity → Level, Velocity → Size) is live on Cloud layers today (last note's
   velocity), so it would move every `inst-*` golden and the instrument presets. The new routes must be
   inert on Cloud layers by construction (voice-only sources, or a route evaluated only per voice), and a
   time-varying one (per-voice Env) must not flip `timeVarying_` for Cloud-only patches (PC:162-175), or
   every cell becomes an applied group (more pieces; should be bit-identical by design, but it is a
   behavior/cost change to verify by hash).
3. **Slot shift from a new layer row**: slots are never saved, but anything keyed by slot index moves:
   Mutate's draws (Mutate.cpp:74-75, 117) — every press changes; any test table indexed by slot. Audio
   goldens don't move.
4. **Note semantics**: today notes fold into `held_` as they arrive and only the post-group set counts
   (PC:1767, PH:352-356). Voices need the group's ordered On/Off list; deriving the Cloud held set from
   voices must reproduce exactly today's per-(channel,key) bit semantics for Cloud layers (including "a note
   inside one cell does nothing", `note_tests.cpp:373`, and two notes on one pair sharing a bit, unless the
   spec deliberately changes Cloud too — that would move goldens only if a golden has overlapping same-key
   notes; check before deciding).
5. **CC 64 on Cloud layers** (program design §4.1: the pedal holds the instrument's held set too): fine at
   rest (no golden sends CC 64), but the held set's derivation must be pedal-aware without changing timing.
6. **Velocity/Key sources**: Cloud layers must keep "last note-on" semantics (PC:780-786); per-voice values
   must not overwrite `pendingMidi_`.
7. **Merge order**: voices' onsets must be merged before `admit` (GP non-decreasing onset requirement);
   a per-voice loop calling `schedule()` voice after voice would feed decreasing onsets and corrupt the
   ledger/steal cursor silently (no assert). Tie-break must be fixed (ordinal), never pointer or slot order.
8. **Voice start/stop on the grid**: start = forced onset at the boundary (as `forceNextOnset`). A voice whose
   release ends must end at a sample fixed by the cell clock (Adsr reaches Idle at a stepped boundary), and
   the voice's last grains then run out their windows; never end a voice at a block end.
9. **Voice steal vs the ladder**: reusing `GrainPool::steal` for the 16-cap steal increments nothing
   unless done in Engine; reporting it through the ladder's `stealSink_` would add `steal` lines that look
   like overload steals. Also a grain already stolen by the ladder can't be stolen again (assert) — the voice
   steal must skip it.
10. **Per-voice parameters per cell**: `sendCloudParams` uses `sameCloud` to skip unchanged sets; each voice
    needs the same, or ruling M1-h's onset rescale (CS:276-285) fires on every cell and moves onsets.
11. **Threads**: per-voice modulation must be evaluated on the calling thread between pieces (as
    modulators are, PH:1018-1022, PC:734-764) and only read inside layer tasks; voice state lives in the
    layer's Engine so a task touches only its layer (PH:240-258).
12. **Event buffer capacity**: `l.events` reserved to 4 × grainCapacity (PC:285-287); voice steals add
    lines; the bound "at most 2 × grainCapacity spawns per piece" (PH:272-274) still holds per pool, but
    check with voice-steal lines.
13. **Instrument tail**: `kInstrumentTailSeconds = 2 + 0.5 + 120` (`GigaGrainsPlugin.h:233-235`) assumes
    no grain after note-off beyond Size; a voice releasing for up to Env Release's 10 s (`envelopeTime`
    max, ParamIds.cpp:87) plus Size lengthens the real tail.
14. **Spectral kind on a Voice layer**: one smear Position per engine (EC:542).
15. **Engine reset/enable**: `Engine::reset` keeps held keys and gate (EH:328-336); `setEnabled(false)`
    clears the pool — both must clear voices too, and a Layer On ramp-off must end voices at the same sample
    it ends Cloud grains (PC:2095-2106 cut the piece there).
16. **UI publishing**: `GrainVis` has a layer only (PC:1446-1448); a voice tint is sub-project 5's, but the
    publish structures must not change size in a way that touches `[publish]` tests' expectations.
17. **Notes past caps**: `kMaxNoteEvents` overflow moves notes to the next boundary (`GigaGrainsPlugin.h:222-225`);
    with voices that changes voice start samples (already documented as non-invariant past the cap).

---

## Appendix: the pieces a Voice layer touches, in call order per applied boundary

`process` (PC:1592) → `join` (1675) → `applyPending` (1629) → `render(..., b)` (2065: `stepSources`,
pieces, quotas 2119-2137, weights, tasks → `renderLayerPiece` (1871) → `Engine::process` (EC:413) →
`processBlock` (EC:478) → `cloud_.schedule` (EC:579) / voice merge → `admit` (EC:672) → `renderPool`
(EC:779)) → `drainEvents` (PC:2177, 1423) → sum A..D (2216-2231) → delay, reverb, limiter, tap) →
`commitPending` (669: `updateKeys` 817) → `stepSources` (766) → `updateEffective` (833: `applyRoutes` 857,
`sendCloudParams` 900) → `syncFreeze` (932) → `retargetRamps` (357) → `applyCommands` (1280).
