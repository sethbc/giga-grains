# M3 sub-project 4: Microtuning

| | |
|---|---|
| **Status** | Draft, written 2026-11-07 while the director asked that every M3 sub-project proceed in parallel without his input. Every forced choice is **Claude's provisional call** (T1 to T15), each with what it declined; the director reviews them later. **T2 (a preset's tuning on load) and T10 (the runtime-loaded libMTS) are the ones most worth his look** |
| **Program** | `docs/superpowers/specs/2026-11-07-m3-program-design.md` (§7, §8 row 4, §9, §10's item 4) |
| **Builds on** | Sub-project 1, the Voice slice (`docs/superpowers/specs/2026-11-07-m3-voice-design.md`, branch `m3-voice`, draft PR #37) |
| **Branch** | `m3-sp4-tuning` (from `m3-voice`), worktree `~/src/gg-m3-sp4-tuning` |
| **Parallel with** | Sub-projects 2 (expression), 3 (pitch-sync) and 5 (views and presets); §9 says where they meet |
| **Research** | MTS-ESP's client and master (`ODDSound/MTS-ESP` at `f214739`) and tuning-library (`surge-synthesizer/tuning-library` at `d6fcc08`), read in full where used; the note path on `m3-voice` (`CloudScheduler::takeNext`, `Processor`'s voice pass) |

## 1. Intent

How a key becomes a pitch, for both modes (Cloud and Voice) and both products, in the program's
order of precedence (§7): **MTS-ESP** when a master is connected; else a **Scala `.scl` and
`.kbm`** the user loaded, saved with the session; else **12-TET**, exactly today's mapping. At
rest (no master, no scale) every golden keeps its WAV SHA-256 and its event log byte for byte. The
director can load a scale in any host from the editor, or follow ODDSound's free MTS-ESP Mini in
the same project, and hear the instrument and the FX's notes in that tuning.

## 2. Scope

**In:**
- the tuning table: the key term of every grain's pitch on Cloud layers and of every voice's on
  Voice layers, in both products (the FX's notes too, D4);
- the Scala path: `.scl` and `.kbm` parsed off the audio thread (tuning-library), the table
  computed deterministically, handed to the audio thread, saved and restored with the session
  (a new `TUNE` chunk) and carried by a preset that names one;
- the MTS-ESP path: the client library, one client per instance, a poller thread off the audio
  thread, the **MTS-ESP** parameter (global 0x38), note filtering;
- unmapped keys (a `.kbm`'s `x`, a master's filter);
- `gg-render`'s `tuning` key (inline text or paths), the packer's, `--save-state`'s `TUNE`;
- four goldens in non-12 tunings and the expected-frequency tests;
- the minimal UI: the MTS-ESP toggle, a **Tuning…** menu and dropping a `.scl` or `.kbm` on the
  editor;
- testing MTS-ESP against a fake master on every leg and against ODDSound's real library on the
  Linux leg.

**Out:** MTS-ESP's multi-channel tables and MTS SysEx (T9); `.ascl` and `.tun` (T14); CLAP's draft
tuning extension (T14); per-layer tunings and a note-on-only option (T6); the polished tuning
chooser and its view (sub-project 5); new factory presets in a tuning (sub-project 5, which may
use the packer's `tuning` key, §5.7).

## 3. Forced choices (Claude's provisional calls; the director may overrule each)

- **T1. One switch, MTS-ESP, decides whether a master wins.** A global parameter **MTS-ESP**
  (Off, On; default On), not automatable, an instance setting like MPE: a preset load and INIT
  keep it (`isMidiSetting`'s list), and Mutate never moves it. With it On and a master connected,
  the master's tuning is in effect; otherwise the Scala tuning loaded, if any; otherwise 12-TET.
  So the program's precedence holds by default, and a user who wants this instance in its own
  scale in a session with a master turns it Off (ODDSound's client notes ask for exactly this
  option, step 7). *Declined:* no switch (a local scale could never win while a master is
  connected); a loaded scale overriding the master automatically (ODDSound's other suggestion,
  but it reverses the approved order of §7).
- **T2. A preset's tuning on load: the sound's when it names one, the session's when it doesn't.**
  A preset that carries a tuning (`TUNE`) applies it on load; a preset that carries none (every
  factory preset, and INIT) keeps the instance's. So a user working in 19-EDO browses the factory
  presets and stays in 19-EDO, and a preset made for a scale brings it. **Save As** writes the
  instance's Scala tuning into the preset when one is loaded. A **session** restore (the host's
  project) is the whole instance: a state without `TUNE` is 12-TET. A master's tuning is never
  saved, in a session or a preset (it is the master's). This is the samples' rule (D5: a preset
  that names no sample keeps the one loaded) applied to the tuning, and unlike P2's MIDI
  settings, which a preset never changes, because a tuning can belong to a sound. *Declined:*
  (a) the instance's setup, like MPE: no preset ever carries a tuning (the program says a preset
  may, and a gamelan preset couldn't bring its slendro); (b) always the sound's: every preset load
  sets the preset's tuning or 12-TET (browsing in a 19-EDO session would snap back to 12 at every
  factory preset). Sub-project 5 may add a "save tuning with preset" choice to Save As; until then
  Save As includes it.
- **T3. The session saves the files' bytes in a new chunk, `TUNE`; no patch-version bump.** A
  binary chunk (layout in §5.4) holding the `.scl` and `.kbm` exactly as loaded and their file
  names, written only when a Scala tuning is loaded. The container stays at version 1 and the
  patch at version 2: an older build skips the unknown chunk and plays 12-TET (and a save from it
  drops the tuning, as any unknown chunk). *Declined:* an element in `PTCH` (Scala files are often
  Latin-1, not UTF-8, and XML would have to escape or re-encode them; PTCH's NUL rule; a tuning
  isn't a parameter); a `MEDI`-style file reference (the files are a few KB, and a missing `.scl`
  would silently retune a project).
- **T4. The table is computed by our own code with deterministic math, not by tuning-library.**
  tuning-library parses the files and validates them (its `Tuning` constructor throws for a
  mapping it can't apply, and we refuse what it refuses, with its message). The table is then a
  port of its mapping arithmetic (`Tuning::Tuning(const Scale &, const KeyboardMapping &, bool)`
  and `withSkippedNotesInterpolated` at the pin) into semitones, with `logDet` in place of
  `log`, no `pow`, and each cents value re-parsed exactly (§5.2). A test checks the port against
  tuning-library over its own corpus (§7). The port is MIT code adapted, under the notice we carry
  for the library. *Declined:* tuning-library's own frequencies: its ratio tones, reference pitch
  and table use libm's `log` and `pow`, whose last bits differ between macOS, Windows and Linux,
  and the grain-event log prints each grain's rate in full, so the cross-platform determinism gate
  would fail on every tuned golden ("no libm value reaches a grain", `CloudScheduler.h`).
- **T5. Pitch is absolute: rate 1 is C4 at A440.** The table holds, per key k, the key term
  s(k) = 12·log2(f(k) / 261.6255653005986), the semitones of the key's tuned frequency above
  12-TET's C4 (440 · 2^(−9/12)); in 12-TET s(k) = k − 60, today's term. So a `.kbm` that puts A4
  at 432 Hz makes every key 31.8 cents flatter, as asked, and a master's frequencies mean what
  they say. An entry within 1e-9 semitones of an integer is that integer, so a 12-EDO scale with
  any 12-TET-equivalent mapping (A4 = 440 at key 69, say), or a master sending 12-TET, renders
  bit-identically to no tuning. *Declined:* relative to the tuned key 60 (key 60 always the
  sample's pitch), which ignores a `.kbm`'s reference frequency and a master's absolute pitch.
- **T6. One tuning per instance, followed continuously, with bends in semitones after it.** The
  tuning applies to every layer, both modes, both products. It is read where the key is read: at
  each Cloud grain's onset (the key the grain drew) and at each voice's cell (the voice's pitch,
  latched per grain as V3 says), so a scale changed under a held chord retunes it grain by grain.
  A channel's bend, an MPE note's bend and a CLAP TUNING expression are added after the tuning, in
  12-TET semitones (a LinnStrument slide is continuous and its range is in semitones). The **Key**
  mod source stays key / 127 (it is the key, not its pitch); **Note Pitch** follows the tuning
  (§5.3). *Declined:* per-layer tunings (one scale is what a session plays in; later if asked); a
  note-on-only option (ODDSound's step 4; later, a carry-over row); bends in scale steps.
- **T7. An unmapped key is silent.** A note-on whose key the tuning in effect leaves unmapped (a
  `.kbm`'s `x`, a key outside its first-to-last range, or a master's `MTS_ShouldFilterNote`) is
  dropped before the note table, in both modes and products, and its `NOTE_END` is queued at once
  (its ID, port, channel and key), so a host's voice tracking closes it. Its note-off finds no note
  and does nothing. A note already held whose key a tuning change unmaps keeps sounding, at the
  interpolated pitch §5.2 gives unmapped keys. *Declined:* playing unmapped keys at an interpolated
  pitch (Scala's `x` means "no note"; ODDSound's step 5 asks clients to filter).
- **T8. The audio thread never calls the MTS-ESP client.** Each instance registers a client on the
  main thread (`init`) and deregisters it there (`destroy`). One **poller thread** per process
  (started with the first registered client, joined after the last) asks each client, every 20 ms
  while it has a master and every 250 ms while it hasn't, `MTS_HasMaster`, then
  `MTS_NoteToFrequency` and `MTS_ShouldFilterNote` for the 128 keys (channel −1), converts the
  frequencies to the table (T5, with `logDet`), and pushes a snapshot to that instance's
  `LatestValue` only when it changed. The audio thread takes the newest snapshot at a call's start:
  lock-free, no allocation, no syscall, as `loadedState_` is today. *Declined:* querying on the
  audio thread, as ODDSound suggests ("as often as possible"): the client's calls go into libMTS, a
  closed binary whose `HasMaster` may touch its IPC, which RTSan can't check in CI and we can't
  read; a host timer (`clap_host_timer_support`), which not every host and wrapper provides. The
  cost: a master's change lands within about 20 ms plus a block.
- **T9. One table per master, channel −1.** The client is always asked with channel −1, the
  master's single-channel table (ODDSound's advice for an MPE client, and this one may be in MPE
  at any time). Multi-channel tables (for controllers with more than 128 keys) and MTS SysEx
  (`MTS_ParseMIDIData`) are later: carry-over rows.
- **T10. libMTS is loaded at run time, never built, linked or shipped** (for the director's
  attention). We pin the MTS-ESP repository and compile exactly its `Client/libMTSClient.cpp`,
  under the 0BSD license (its text is SPDX's 0BSD word for word), which is AGPLv3-compatible.
  That file `dlopen`s ODDSound's own `libMTS` (`/Library/Application Support/MTS-ESP/libMTS.dylib`,
  `/usr/local/lib/libMTS.so`, `Program Files\Common Files\MTS-ESP\LIBMTS.dll`) when the user has
  installed it (MTS-ESP Mini's installer does), and works without it (12-TET). libMTS is
  ODDSound's free but closed-source binary; the products never carry it, and the interface is a
  narrow C one (a table of 128 doubles, a filter, a name). Surge XT, under GPL-3.0 and by the
  authors of our GPL sst libraries, ships this same client the same way, as does Vital; we follow
  that precedent. *Declined:* no MTS-ESP (the program's §7 asks for it); writing our own client
  (the same `dlopen`, minus the maintained code).
- **T11. The signed macOS standalone gets no library-validation exception.** A plugin runs in its
  host's process, whose entitlements decide whether ODDSound's library may load (DAWs allow plugin
  libraries). Our notarized standalone apps run under the hardened runtime with library validation,
  so `dlopen` of ODDSound's dylib fails there and the app plays its Scala tuning or 12-TET.
  ODDSound's README says a standalone needs `com.apple.security.cs.disable-library-validation`;
  we don't add it (it opens the app to every unsigned library for one niche case). A carry-over
  row. *Declined:* adding the entitlement.
- **T12. The minimal UI: two controls in the Midi panel and a drop.** The FX view's Midi panel (M3
  sub-project 1's) gains the **MTS-ESP** toggle (a parameter, so "every parameter is reachable"
  needs it) and a **Tuning…** button (ID `tuning.menu`, caption fixed so it never elides). Its
  menu: a disabled first line naming what is in effect ("12-TET", "Scale: 19-EDO (19-edo.scl,
  a440.kbm)", "MTS-ESP: <the master's scale name>", or a load failure); **Load scale (.scl)…**;
  **Load keyboard mapping (.kbm)…**; **12-TET** (clears both); and, when a master is connected but
  the switch is Off, "MTS-ESP master connected (ignored)". A `.scl` or `.kbm` dropped anywhere on
  the editor (the loader strip's and the canvas's drop targets included) loads it. A load isn't an
  undo entry (like the memory cap) and marks the state dirty; a failure says why in the editor's
  notice ("can't load 19-edo.scl: <tuning-library's message>") and changes nothing. *Declined:*
  waiting for sub-project 5's chooser (the director couldn't test the sub-project in a host).
- **T13. MTS-ESP is tested twice.** On every leg and under the sanitizers, a test hook pushes
  snapshots as the poller would (precedence, filtering, the switch, the tripwire). On the Linux leg
  only, a separate CI step after the main tests installs ODDSound's `libMTS.so` from the pinned
  submodule (its SHA-256 checked, §6) and runs the real-master test: a master registered in process
  through `Master/libMTSMaster.cpp` (0BSD too), our instrument created through the CLAP factory,
  the master's 19-EDO heard in the grains' rates, then its 12-TET bit-identical to no tuning, then
  its deregistration falling back to the loaded scale. *Declined:* the fake alone (the real
  client's loading and polling would go unexercised); libMTS installed for every test (a closed
  binary in the sanitizer jobs, and libMTS's IPC would let one test's master retune another test's
  plugin running beside it).
- **T14. `.scl` and `.kbm` only.** tuning-library also reads Ableton's `.ascl`; AnaMark `.tun` and
  CLAP's draft `clap.tuning` extension (host-provided tunings) would be new paths. All three are
  later: carry-over rows.
- **T15. The pins.** tuning-library at **`d6fcc08`** (2026-10-19, `main`'s head): its release tags
  date from 2021 and lack the bounds fixes for hostile `.kbm` files (#92, #95: a distant tuning
  note, an unmapped center) that matter for files a user downloads. MTS-ESP at **`f214739`**
  (2026-02-04, `main`'s head; its last code change is `dbdf46f`, 2025-12-04): its only tags name
  libMTS binary releases (`libMTS-v1.03`), not the client.

## 4. The model

### 4.1 Where the key term is used today, and what replaces it

| Path | Today | With the tuning |
|---|---|---|
| Cloud grain (`CloudScheduler::takeNext`, both products; the FX's D4 notes too) | `static_cast<double>(note - 60)` added to `pitchSemitones` | `keySemitones[note]` |
| Voice (`Processor`'s voice pass, `pitchIn`) | `(static_cast<double>(note.key) - 60.0) + bend + tuning` | `keySemitones[note.key] + bend + tuning` |
| Note Pitch source (per voice and the newest held note's) | `(key + bend + tuning) / 127` | `(60.0 + keySemitones[key] + bend + tuning) / 127` |
| Key source | `key / 127` | unchanged (T6) |

In 12-TET `keySemitones[k]` is exactly `k − 60.0`, so each expression computes the same double
from the same operands in the same order: every render at rest is bit-identical by construction
(`60.0 + (k − 60.0)` is exactly `k`). The ±48 clamp on a grain's total pitch is unchanged; a
tuning's key term is clamped with the rest.

### 4.2 The table

`gg::core::TuningTable` (`core/include/gg/core/Tuning.h`), trivially copyable:
- `std::array<double, 128> keySemitones`: s(k) of T5, each finite and within ±1024;
- `std::array<uint64_t, 2> unmapped`: bit k set when key k's note-on is dropped (T7);
- `static constexpr double kC4Hz = 261.6255653005986`;
- `twelveTet()`: the identity table, k − 60 and nothing unmapped.

The engine can't include gg-core, so `engine/include/gg/grain/KeyTuning.h` holds the engine's view:
`kTwelveTetKeySemitones` (a `constexpr std::array<double, 128>`) and a `const double *` that
`CloudScheduler` reads through (default: that constant). Each `Engine` is given the `Processor`'s
table's address once (`EngineConfig::keySemitones`); the voice streams, which have no held keys,
never read it.

### 4.3 What is in effect

The `Processor` holds two inputs and resolves them at a cell boundary, the call's first:
- `local`: the Scala table, or `twelveTet()`, set by `setTuning(const TuningTable &)` (before
  activation, from `gg-render`, or from the plugin's audio thread when it takes a load);
- `mts`: `MtsTuning {TuningTable table; bool connected;}`, set by `setMtsTuning` (the plugin's
  audio thread, from the poller's snapshot; never in `gg-render`).

Effective = `mts.connected && MTS-ESP On ? mts.table : local`. When the effective table's bytes
change, the `Processor` copies it into the engines' shared table (1 KB, once per change) and marks
the voice caches stale (each voice's `pitchIn` compares bit for bit already, so a changed key term
recomputes it). Nothing else changes: a tuning change lands at the call's first sample, before the
call's events, so `gg-render` (which sets its tuning before the render) is block-size invariant and
the plugin's change lands as a UI edit does.

### 4.4 Unmapped keys

The `Processor`'s note intake checks a note-on against the effective table's `unmapped` bits before
the note table (T7) and, on a hit, queues an `EndedNote` (sub-project 1's `ended_`, so the plugin
emits `CLAP_EVENT_NOTE_END`). An unmapped key's `keySemitones` entry is interpolated (§5.2), never
read by a new note, only by a held one that a change unmapped.

## 5. Design

### 5.1 Parameters (append-only IDs)

- **MTS-ESP**, global, stepped, 0 Off, 1 On, default **On**; CLAP ID **0x000038** (sub-project 4's
  block, global 0x38 to 0x3F; sub-project 2 takes 0x25 upward, sub-project 3 0x30 upward). Flags:
  stepped, not automatable (as MPE). Display "MTS-ESP"; `gg-render` accepts `"MTS-ESP"` in `params`
  and it does nothing there (no master).
- No layer parameter (the block 0x3C upward stays unused).
- `kParamCount` 290 → 291 (plus the other sub-projects' as they merge); `kNextFreeGlobalIndex`
  becomes 0x39 (one past the largest below the reserved blocks; a merge with sub-project 2 or 3
  keeps it the largest of all, so the later merge recomputes it, it never collides). The counts
  that follow (`tests/support/ParamTable.h`, `patch_tests`, `identity_tests`, `params_tests`,
  `rt_tests`, `EditLayout` and its tests) are updated. Mutate is keyed by CLAP ID (sub-project 1),
  so no press moves; MTS-ESP joins its locks and `isMidiSetting` (kept by `keepRingParams` and
  `keepRingLengths`, so by every preset load and INIT). Remote-controls pages don't list it.
- No patch-version bump: a state without the value loads On.

### 5.2 Scala: parse, validate, compute

`gg::core::tuningFromScala(std::string_view scl, std::string_view kbm) ->
std::expected<TuningTable, std::string>` (`core/src/Tuning.cpp`, the only file that includes
tuning-library; gg-core links it privately, SYSTEM includes, as `gg::tuning-library`). Off the
audio thread only (it allocates and may throw inside; it catches everything and returns the
message). Empty `scl` means tuning-library's 12-EDO scale; empty `kbm` means the standard mapping
with **key 60 at `kC4Hz` exactly** (not tuning-library's `MIDI_0_FREQ · 32`, whose bits differ), so a
12-EDO `.scl` alone gives k − 60 with no snapping.

1. **Refuse** a text over 64 KiB or holding a NUL, before parsing.
2. **Parse** with `Tunings::parseSCLData` and `Tunings::parseKBMData`, and **validate** by
   constructing `Tunings::Tuning(scale, mapping)`: any `TuningError` (or other exception) refuses
   the load with its message.
3. **Re-parse each cents tone exactly:** from the tone's `stringRep`, up to a `!`, trimmed, an
   optional sign, digits, `.`, digits (Scala's form; trailing text after the number ignored). With
   N the digits as an integer and f the count after the point, the tone's semitones are one
   correctly rounded division, ±N / 10^(f+2), when N < 2^53 and f + 2 ≤ 22; otherwise the trailing
   digits past 15 significant ones are dropped first (under 1e-12 cents). A ratio tone n/d (int64)
   is 12 · logDet(double(n) / double(d)) / logDet(2). A `.kbm`'s reference frequency is parsed the
   same exact way as Hz. So no value depends on the platform's `strtod`, `log` or locale.
4. **Map** each key 0 to 127 by porting tuning-library's constructor at the pin (the window walk to
   the tuning note, the KBM rotations, octave degrees, the tuning-center offset, the scale's period
   as its last tone), carried out in semitones from the tones of step 3, with the reference term
   12 · logDet(F / kC4Hz) / logDet(2) for a reference frequency F at the reference key. Additions
   in the order the port states them, under `GG_FP_CONTRACT_OFF`. Unmapped keys are marked and then
   interpolated as `withSkippedNotesInterpolated` does (linear between the nearest mapped keys, by
   key number; at the ends, the nearest mapped key's value plus one semitone a key). A mapping with
   no mapped key at all is refused.
5. **Snap** each entry within 1e-9 of an integer to it (T5), then clamp to ±1024.

### 5.3 The audio side

- `CloudScheduler::takeNext` reads `keySemitones_[note]` where it computed `note - 60` (both
  branches: with and without spreads).
- The `Processor`'s voice pass and `updateNoteSources` read the table through one helper,
  `double Processor::notePitch(const NoteTable::Note &) const noexcept` = key term + bend +
  tuning expression, used for `pitchIn` and, plus 60 and over 127, for Note Pitch. **Sub-project 3
  computes a voice's frequency from this helper** (kC4Hz · exp2Det(notePitch / 12)), so pitch-sync
  follows the tuning, slides and expressions for free (§9).
- The unmapped check of §4.4 sits in the note intake, before `NoteTable::noteOn`, in both products.
- `setTuning` and `setMtsTuning` copy (a `TuningTable` is about 1 KB) and set a flag; the resolve
  and the engine copy happen at the next cell. Real-time: bounded copies, no allocation, no lock;
  the tripwire tests run with tunings loaded mid-process.

### 5.4 The `TUNE` chunk (T3)

Written by `encodeState` after `INST` when `SessionState::tuning` holds a Scala tuning (either
file); read by `decodeState`. Little-endian, as the container:

```
u32 version (1)
u32 sclLength, then sclLength bytes   (the .scl as loaded; 0: 12-EDO)
u32 kbmLength, then kbmLength bytes   (the .kbm as loaded; 0: the standard mapping)
u32 sclNameLength, then the bytes     (UTF-8 file name for display, at most 255; may be 0)
u32 kbmNameLength, then the bytes     (likewise)
```

- `SessionState` gains `std::optional<TuningFiles> tuning` (`scl`, `kbm`, `sclName`, `kbmName`,
  as `std::string` bytes) and `std::optional<std::string> newerTuning`.
- **Reading:** a length running past the payload, a text over 64 KiB or a name over 255 bytes
  fails the whole state (all or nothing, as a malformed chunk does); a repeated `TUNE` fails it.
  A `version` above 1 loads as 12-TET with the payload kept in `newerTuning` and written back byte
  for byte (as a newer `MEDI` is), status "tuning from a newer version". A version-1 payload whose
  texts `tuningFromScala` refuses loads the rest of the state, plays 12-TET, keeps the bytes and
  writes them back (a later build may read them), status "the session's tuning didn't load: <the
  message>". `decodeState` checks only the layout and the limits; the plugin and `gg-render` then
  call `tuningFromScala` on the texts, so a refusal there is a status, never a failed state.
- An older build skips `TUNE` as an unknown chunk and plays 12-TET; it saves without it.
- `presetInfoOf` and the catalog ignore it.

### 5.5 Loads, restores and presets (T2)

| Load | With a `TUNE` | Without |
|---|---|---|
| Session restore (`state.load`) | its tuning | 12-TET |
| Preset file (`.ggpreset`, browser, discovery, VST3 program, AU preset) | its tuning | the instance's kept |
| INIT | (none carries one) | the instance's kept |
| `gg-render`: `state`, then `preset`, then the spec's `tuning` | each replaces the one before | kept |

The plugin applies the tuning through the path a load uses: `applyState` computes the table on the
main thread, and while active it rides `loadedState_` (`LoadedState` gains `TuningTable tuning` and
`bool hasTuning`), so a restore's values and tuning reach the audio thread in one call; an editor
load while active rides its own `LatestValue<TuningTable>` (as `uiRoutesPending_` does for routes),
taken after `loadedState_`. While inactive the table waits on the main thread and is given at
`activate()`. A preset load doesn't mark the preset edited by its tuning, and a tuning load from
the editor doesn't either (the edited mark compares values).

### 5.6 MTS-ESP in the plugin (T1, T8, T9, T13)

- `libs/MTS-ESP` (submodule, `f214739`); target `gg_mts_client` (STATIC, `Client/libMTSClient.cpp`
  alone, in `libs/CMakeLists.txt`, without `gg::compiler-options`, its include SYSTEM, `dl` on
  Linux), linked privately by `gg-plugin-core` only: gg-core, gg-engine, gg-media and the tools
  never see it. Its static initializer `dlopen`s libMTS when the plugin binary loads (the host's
  thread); that is ODDSound's design and touches no audio thread.
- `plugin/src/MtsPoller.{h,cpp}`: the process-wide poller (T8). An instance's entry is its
  `MTSClient *`, its `LatestValue<MtsTuning>` (producer: the poller) and a mutex-guarded status
  for the editor (connected, the master's scale name copied to 64 bytes). Register and deregister
  under the poller's mutex, on the main thread; the poller stops using a client before
  `MTS_DeregisterClient`. Frequencies that aren't finite and positive keep that key's 12-TET term.
- **The seam:** with `GG_NO_MTS_ESP` set and not empty, an instance registers no client and the
  poller never starts. `tests/plugin/PluginHarness.h` sets it, as it sets `GG_SETTINGS_FILE`, so no
  plugin test sees a master on the machine (libMTS's IPC reaches across processes: a DAW with
  MTS-ESP Mini open would otherwise retune the tests). `gg-snap` sets it too.
- **The test hook** (`PluginTestHooks.h`): `pushMtsTuning(plugin, MtsTuning)` feeds the instance's
  `LatestValue` as the poller would, with the seam on.
- **The real master** (Linux leg only): `tests/plugin/mts_master_tests.cpp`, built into its own
  executable `gg-mts-master-tests` with `libs/MTS-ESP/Master/libMTSMaster.cpp`, labels `plugin`,
  `mts` and `serial`. It skips (Catch2 `SKIP`) unless libMTS is at the client's path, and with
  `GG_REQUIRE_MTS_ESP=1` a missing libMTS fails instead (as `GG_REQUIRE_AVX2` does).

### 5.7 `gg-render` and the packer

- **`tuning`** (FX and instrument mode; an unknown key in sample mode, which has no notes):
  `{"scl": "<text>"}` or `{"sclFile": "<path>"}`, and `{"kbm": "<text>"}` or `{"kbmFile":
  "<path>"}`, each optional, at least one present, a text and a file of one kind together an error.
  Paths resolve as a file source's (the spec's directory or `--media-dir`). A refusal is bad input
  (exit 2) with `tuningFromScala`'s message. It replaces a `state`'s or a `preset`'s tuning (§5.5).
  Inline texts keep the goldens self-contained (no committed tuning files; the expected-frequency
  tests build theirs in code).
- `--save-state` writes `TUNE` when the render had a Scala tuning (file names: the paths' last
  components, or empty for inline text).
- **The packer** (`--pack-preset`): a source's optional `tuning`, the same shape, paths relative to
  the source file; it writes `TUNE` into the preset. `tests/fixtures/presets/` gains
  `inst-tuning-test.json` (inline 19-EDO).
- `RenderSpec.h` documents the key; `render_tuning_*` tests pin the refusals (both forms given, an
  empty object, a bad `.scl`, a `.kbm` that doesn't fit, a missing file, sample mode).

### 5.8 Goldens (new; each catches the 16 injected steps or joins the table with its reason)

| Case | What | Why |
|---|---|---|
| `inst-tuning-19edo` | Mode Cloud; a 220 Hz sine sample; inline 19-EDO `.scl`; keys 60 to 79 one at a time (0.3 s each), then a three-note chord; 8 s | a non-12 equal scale on Cloud grains; the octave at key 79 |
| `inst-voice-just` | Mode Voice; a 12-tone 5-limit just scale (ratios) with a `.kbm` putting A4 at 432 Hz on key 69; a pedaled chord, then one MPE note on channel 2 sliding +2 semitones of bend; 8 s | ratio tones, an absolute reference, Voice pitch, bend added after the tuning |
| `fx-tuning-unmapped` | the FX over `loop-48k.wav`, D4's note freeze; a 5-tone pentatonic `.scl` mapped to the white keys by a `.kbm` with `x` on the black keys; notes on white and black keys | the FX's notes in a tuning; unmapped keys silent (their spans render as no new grains) |
| `inst-tuning-bp` | Mode Voice; Bohlen-Pierce (13 steps of the 3/1 tritave, cents); a rising phrase across a tritave; 6 s | a period other than the octave |

Each has its spec in `tests/golden/cases/` and expected files; their event logs are compared
exactly across platforms (T4 makes that possible). `click_injection_tests.cpp` adds each, or pins a
count with its reason in the same commit.

## 6. Dependencies, licensing and CI

- **Submodules:** `libs/tuning-library` (https://github.com/surge-synthesizer/tuning-library,
  MIT, `d6fcc08f938cc2ad812b82f83e59e2b4070afdb7`; header-only: `include/Tunings.h`,
  `TuningsImpl.h`, `TuningsConcepts.h`; C++20 concepts, fine at C++23) and `libs/MTS-ESP`
  (https://github.com/ODDSound/MTS-ESP, 0BSD, `f214739b8832e7f297cb9970d0c0efbf783f1462`). Neither
  has nested submodules we build (tuning-library's `libs/catch2` is never compiled).
- **`THIRD_PARTY_NOTICES.md`:** tuning-library in the MIT table ("Copyright 2019-2020, Paul Walker",
  `libs/tuning-library/LICENSE.md`, both products; also our port of its mapping arithmetic,
  `core/src/Tuning.cpp`); a section for the MTS-ESP client with its 0BSD text and copyright
  (0BSD requires no notice; we carry it anyway), stating that libMTS is ODDSound's separate
  program, not distributed. `notices_tests.cpp`'s embedded texts follow, byte for byte.
- **The installers** stage nothing new (libMTS is the user's install).
- **CI, Linux leg:** after the main test step, a step "MTS-ESP against ODDSound's libMTS": checks
  `libs/MTS-ESP/libMTS/Linux/x86_64/libMTS.so` against its SHA-256
  (`03d23cb8d64ae246ceb416a7323cb230c5748c4b58d52a7c288d2c93ea3b7de5` at the pin; the workflow pins
  it, so a submodule move that changes it fails the step), installs it to `/usr/local/lib`, and runs
  `ctest --preset linux-gcc13 -L mts` with `GG_REQUIRE_MTS_ESP=1`. Nothing else runs with it
  installed. The sanitizer jobs and the other legs never install it (the master test skips there).
- **INTENT §15's table:** the MTS-ESP client and tuning-library get their pins, "0BSD" for "0BSD-
  style", and the run-time libMTS sentence (T10).
- **`docs/dependency-watch.md`:** nothing (both pins are their heads).

## 7. Tests (label `tuning` for the new Catch2 cases)

- **Core, the table** (`tests/core/tuning_tests.cpp`): a 12-EDO `.scl` alone, and with a `.kbm`
  putting 440 Hz on key 69, give exactly `twelveTet()`; 19-EDO, Bohlen-Pierce and a just scale
  against independent formulas (libm, within 1e-12 semitones); the exact cents parse (signs,
  trailing text, comments, long digit strings, a locale with a comma decimal point set around the
  call changes nothing); refusals (64 KiB, NUL, no notes, a mapping larger than the scale, a tuning
  note on an unmapped key, no mapped key) carry tuning-library's message; unmapped bits and the
  interpolation; **the port against tuning-library**: for every `.scl` × `.kbm` pair in
  `libs/tuning-library/tests/data` (and each `.scl` alone) that tuning-library accepts, the same
  mapped keys and, on each, |s(k) − 12 · log2(frequencyForMidiNote(k) / kC4Hz)| < 1e-9; a pair it
  refuses, we refuse.
- **Core, the Processor** (`tests/core/tuning_processor_tests.cpp`): Cloud grains' rates and voice
  grains' rates equal exp2Det(clamp(Pitch + s(k) …) / 12) for each key; bend and a TUNING
  expression add after the tuning; Note Pitch follows it, Key doesn't; an unmapped note-on starts
  nothing, queues its `NOTE_END` and its note-off is a no-op; a held note unmapped by a change keeps
  sounding; a mid-note change retunes the next grains; precedence (MTS connected and On, connected
  and Off, disconnected, local absent); block-size invariance with a tuning (`determinism`).
- **State** (`tests/core/session_state_tests.cpp`, `preset` tests): `TUNE` round-trips bit for bit;
  absent writes none; the layout refusals; a newer version kept and written back; an unparseable
  version 1 kept, written back, 12-TET; an older-build simulation (unknown chunk) ignores it.
- **Plugin** (`tests/plugin/tuning_tests.cpp`): an editor load reaches `process` at the next call;
  save and restore; a preset with `TUNE` applies it, one without keeps, INIT keeps, a session
  without it is 12-TET; the MTS-ESP parameter is kept by a preset load and INIT; the fake master
  through the hook (precedence, the switch, filtering, `NOTE_END` emitted); the seam; the tripwire
  (`rt`) with a tuning and MTS snapshots pushed during `process`; the frame tests with the two new
  controls (both themes, both sizes, keyboard reach, "every parameter is reachable").
- **The real master** (§5.6), Linux leg.
- **gg-render:** `render_tuning_*` (§5.7); the packer's `tuning`; `--save-state` writes `TUNE`.
- **Goldens:** the four of §5.8, and **the expected frequencies**: an in-process render of each
  checks every grain's `rate` against the ratio its key's independently computed pitch gives
  (within 1e-12 relative), and `inst-tuning-19edo`'s output, over each sustained note's middle
  0.25 s, has its spectral peak (pffft, parabolic interpolation) within 1 cent of 220 Hz times that
  ratio.
- **At rest:** `scripts/update-goldens.sh` before and after; every existing golden's WAV SHA-256 and
  event log unchanged (the 108, sub-project 1's eight, and whatever sibling sub-project has merged),
  stated in the commit; and an explicit 12-EDO `tuning` added to `inst-chord`, `fx-note-freeze` and
  `inst-voice-chord` renders their WAVs bit for bit (a test).
- `scripts/check.sh` (dev, asan, rtsan), the validators (clap-validator's preset crawl sees the
  fixture preset with `TUNE`), and `gg-bench`'s headline ratios (the Cloud path now loads its key
  term from a table: no change expected, measured).

## 8. INTENT changes (Part II; one §23 row, the next free number when this merges)

- **§14:** the key term and the tuning table (§4); precedence and the MTS-ESP switch; unmapped keys;
  "Note Pitch follows the tuning, Key doesn't"; the poller and the audio thread's lock-free take.
- **§15:** the two pins and their licenses in the table (T15, T10); `gg_mts_client` in
  `gg-plugin-core` only; libMTS loaded at run time and never distributed; the macOS standalone
  (T11).
- **§17:** the Midi panel's MTS-ESP and Tuning… (T12); `.scl` and `.kbm` drops.
- **§18:** the `TUNE` chunk (§5.4); what a load does with a tuning (§5.5, T2); MTS-ESP kept by preset
  loads and INIT.
- **§19:** the tuned goldens and the expected-frequency check; the real-master step; `GG_NO_MTS_ESP`
  and `GG_REQUIRE_MTS_ESP`.
- **§23 row:** "M3 sub-project 4, Microtuning: T1 to T15, Claude's provisional calls of 2026-11-07,
  for the director's review" with T2 and T10 named.
- **CLAUDE.md:** the `tuning` and `mts` labels, `gg-render`'s `tuning` key, the `TUNE` chunk, the
  seam in the parallel-safety list, the Linux leg's MTS-ESP step.
- **Carry-over rows** (`docs/m3-carry-over.md`): MTS-ESP multi-channel tables and SysEx (T9);
  `.ascl`, `.tun` and `clap.tuning` (T14); a note-on-only tuning option and per-layer tunings
  (T6); the signed macOS standalone without libMTS (T11); Save As's "include tuning" choice (T2,
  sub-project 5).

## 9. Where the parallel sub-projects meet this one

- **Sub-project 1 (merged under all of them):** the voice pass's `pitchIn` and `updateNoteSources`
  are the lines §4.1 changes; `ended_` carries the unmapped notes' `NOTE_END`; the Midi panel is
  its.
- **Sub-project 2 (expression):** MPE bends (member and master, with its RPN ranges) and CLAP TUNING
  are added after the tuning, in semitones (T6); nothing of 2's changes the table. The MTS-ESP
  client is asked with channel −1 whatever the MPE zones (T9). If 2 adds rows to the Midi panel,
  the later merge renumbers the panel's rows (the layout table is no contract). IDs: 2 takes global
  0x25 upward, this sub-project 0x38.
- **Sub-project 3 (pitch-sync):** a Pitch-clock voice's grain rate is its note's frequency; 3 must
  take it from the tuned note pitch, `Processor::notePitch` (kC4Hz · exp2Det(notePitch / 12)), not
  from the key (440 · 2^((k − 69)/12) would ignore the tuning). Whichever merges second moves the
  helper or `kC4Hz` (`gg/core/Tuning.h` owns it) so there is one of each. IDs: 3 takes global 0x30
  and layer 0x38 upward; no overlap with this sub-project's global 0x38 (different scopes).
- **Sub-project 5 (views and presets):** its tuning chooser replaces or extends T12's menu and keeps
  the drop; it may add Save As's tuning choice (T2); new factory presets may carry a tuning through
  the packer's `tuning` key (their goldens then pin it).

## 10. The builder check (the questions a builder would still ask, answered)

1. *Why not just call `MTS_RetuningInSemitones` per grain?* T8: the audio thread never enters the
   client; the poller's snapshot is the only MTS input the audio thread sees.
2. *Does the table live per layer?* No: one per `Processor`, written on the audio thread at a cell,
   read by every layer's Cloud scheduler through a pointer (§4.2). Voice streams don't read it.
3. *Where does `kC4Hz` come from, and why not tuning-library's?* T5 and §5.2: the literal
   261.6255653005986 (the double nearest 440 · 2^(−9/12)), so the default mapping is exact.
4. *What if a `.kbm` is loaded with no `.scl`?* The scale is 12-EDO (tuning-library's), mapped by
   that `.kbm`: "A4 = 432" works alone. `TUNE` then has an empty `scl`.
5. *Load `.scl` while a `.kbm` is loaded?* It replaces the scale and keeps the mapping; a pair
   tuning-library refuses refuses the new file (nothing changes), the notice saying which.
   **12-TET** clears both.
6. *What does the session hold while a master is in effect?* The local Scala tuning (or none); the
   master's is never written (T2).
7. *The FX's D4 notes with an unmapped key?* Dropped like any note (T7): no freeze, no repitch.
8. *Can the tuning be automated or changed by MIDI?* No (no parameter carries it; SysEx is T9's
   later item). The MTS-ESP switch is not automatable.
9. *Exceptions in gg-core?* No target disables them (`cmake/` sets no `-fno-exceptions`);
   `Tuning.cpp` catches tuning-library's and never lets one out: callers get `std::expected`.
10. *Is a master's change block-size dependent?* It lands at the first sample of the call after the
    poller pushed it, as an editor edit does; host timing, not the render. `gg-render` has no master
    and sets its tuning before the render, so every golden is block-size invariant.
11. *What does the poller do when libMTS isn't installed?* `MTS_HasMaster` returns false (the
    client's function pointers are null); it polls at 250 ms and does nothing else. The thread is
    the only cost (four wakeups a second per process).
12. *Status text for the UI?* `GigaGrainsPlugin::tuningStatus()` (main thread): what is in effect
    (12-TET, Scale with the two file names, MTS-ESP with the master's scale name), whether a master
    is connected, the last load's failure. The editor polls it on its existing timer.
13. *Tuned goldens' click injection?* As any new case: all 16 or a pinned count with the reason.
14. *Does the `.scl`'s text show anywhere?* Its description line (the first non-comment line) is
    the name the menu shows when it isn't empty; else the file name.
15. *Does a `.kbm`'s reference key on an unmapped key load?* No: tuning-library refuses it (its
    default, `allowTuningCenterOnUnmapped` false), and so do we.

## 11. What's left to the builder

The plan's task order (each its own commit and review, per the program's §9): the submodules,
targets and notices; `Tuning.h`/`Tuning.cpp` with the port and its corpus test; the engine's table
pointer and the `Processor`'s resolve, intake filter and `notePitch` (the at-rest proof here:
`update-goldens.sh` before and after); `TUNE` and the load rules; `gg-render`'s key, the packer and
the four goldens with the expected-frequency test; the plugin's local load, the MTS-ESP parameter,
the poller, the seam and the hook; the minimal UI and the frame tests; the CI step and the real
master test; INTENT, CLAUDE.md and the carry-over rows. The exact contents of the four golden
specs (the scales' texts, note times) and the poller's file layout are the builder's.

## 12. Addendum (2026-11-07): Pitch Quantize (T16)

A scope addition from the top controller, provisional for the director: a competitor comparison
(the S-4's Mosaic and the GR-MEGA's pitch quantize) found grain pitch quantized to a scale missing,
though INTENT §8.1 names "scale quantize" as how randomness is shaped and §22 left it at "after
M2". The tuning table already holds the scale.

- **T16. A per-layer Pitch Quantize (Off, Scale; default Off).** A layer parameter, stepped, 0 Off,
  1 Scale, CLAP layer index **0x3C** (this sub-project's layer block, 0x3C to 0x3F), in all four
  layers: 4 parameters, so `kParamCount` 291 → 295 and `kNextFreeLayerIndex` 0x3D. Automatable
  like Quality; not a mod target; Mutate follows Quality's rule for a stepped layer parameter.
  - **What snaps.** With it on, a grain's semitones before Smear's micro-detune (the key term, the
    layer's Pitch with its modulation, and Pitch Spread's draw) snap at the grain's onset to the
    nearest **degree** of the effective tuning; Smear's detune is then added and the ±48 clamp
    applied as today. The degrees are the sorted, distinct `keySemitones` of the effective table's
    mapped keys (12-TET: the integers −60 to 67, so semitones). A tie goes to the lower degree; a
    value past the lowest or highest degree snaps to it. In both modes (Cloud and Voice), both
    products (the FX's cloud without a key: the same snap of its Pitch and spread).
  - *Ruling (provisional):* the layer's Pitch, base and modulation, snaps with the spread, because
    the engine sees one pitch value per grain; a transpose by one of the scale's intervals (an
    octave, in any octave-repeating scale) is unchanged by it. *Declined:* snapping the random and
    modulated part alone around an unsnapped base (needs the base split from its modulation in the
    engine); a period-repeating degree set beyond the 128 keys (the ends clamp; a carry-over row).
  - **Off is exactly today:** no snap, no extra draw, so every golden at rest is bit-identical.
  - **The engine** gets the degrees through `EngineConfig` (a pointer to a sorted array and its
    count, owned by the `Processor` beside the key table, rebuilt in `resolveTuning` when the
    effective table changes): a binary search per grain, bounded, no allocation.
  - **Tests:** a quantized Pitch Spread lands only on scale degrees (12-TET and a 5-note scale);
    Off bit-identical; block-size invariance; one golden, `inst-quantize-pentatonic` (a 5-note
    `.scl`, Pitch Spread 12, Pitch Quantize Scale, one held note), its grains' rates each a degree.
