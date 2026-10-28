# M1 sub-project 3: Sources — design

| | |
|---|---|
| **Milestone** | M1 Cloud core (INTENT §20), sub-project 3 of 6 |
| **Status** | Approved by the director, 2026-10-28. Decisions D1–D8 made by the director, 2026-10-28, by forced choice |
| **Spec authority** | `INTENT.md`; this document records how this sub-project implements it |
| **Builds on** | The FX-slice spec §1 (scope), the Cloud-engine spec (the SoA pool, the SIMD kernels, the interleaved source) |
| **Starts from** | `main` at `92ad605` (sub-project 2 merged as `c2bd43d`, then the `.gitignore` change) and `docs/m1-carry-over.md` |

## 1. Why this sub-project

INTENT §23 #35 gives sub-project 3 three things:

1. **The Sample source,** which gives the instrument its sound. Today Giga Grains runs the engine in
   Sample mode with no source, so it is silent; it only counts note-ons.
2. **Note-triggered freeze/capture in the FX** (§23 #25, #32): a headline MIDI-driven FX feature
   that INTENT names but never defines.
3. **The state's media manifest** (§18), so a session remembers its sample and finds it again.

It also owns seven carry-over items, and several more fall due because it touches the state format,
the loader and `gg-render` (§10).

**Who it's for.** The director, playing Giga Grains from a MIDI track in Ableton Live 12 or Bitwig
Studio with a sample of their own, and playing notes into Giga Grains FX to hold and repitch what
the track just played. Both must work in those hosts at the end of the sub-project, not only in
`gg-render`.

## 2. Goals

- **The instrument plays a sample from notes.** A user loads a file into Giga Grains, plays notes,
  and hears a Cloud texture of that file repitched to the notes; a held chord becomes a chord cloud.
- **The FX plays from notes.** A note into Giga Grains FX freezes what the track just played and
  repitches the grains to the held notes; releasing the notes lets the effect follow its input
  again.
- **A session finds its sample again.** A project reopened on the same machine, or on another one
  with the file moved, restores the sample when it can be found, and says plainly when it can't.
- **Loading never disturbs audio.** Decoding, conversion, hashing and searching happen off the
  audio thread; a new sample swaps in without a click.
- **Everything stays deterministic.** Note-driven renders, instrument and FX, are bit-identical at
  any host block size, and their grain-event logs match across macOS, Windows and Linux.
- **`gg-render` tests what a DAW plays:** an instrument mode driven by notes, notes in FX mode, and
  a WAV-file input for FX mode (ruling M1-aa).

## 3. Non-goals and anti-goals

**Non-goals** (each is later work, named where INTENT places it):

- Polyphony, per-note grain streams, MPE and note expressions: the Voice scheduler, M3.
- Velocity, the sustain pedal (CC 64) and a settable root note: notes carry pitch only (D6).
- More than one sample per instance (§4's "possibly several at once") and the four layers: M2.
- A process-wide sample pool shared across instances, the per-instance memory cap and its readout:
  M2 (§14 "Memory management").
- Embedded audio (`AUDI`), captures saved with the session, live-ring persistence, and the
  "collect and save" command (§18): M2, with minutes-long buffers. `AUDI` is reserved here (§6.6).
- Mipmaps for upward transposition (§14): M2. Until then, notes far above 60 alias (§9).
- The waveform lane and the drop target on the canvas: sub-project 5. This sub-project's loader is
  a minimal one in today's placeholder editor (D3).
- A Quality parameter or HQ tier: M2 (§23 #43).

**Anti-goals** (it fails if any of these happens, even if everything else works):

- The audio thread waits on, or allocates for, a load, a search or a state change.
- Loading a sample, or releasing a note, clicks.
- A reopened project plays a different file than it saved without saying so.
- A state saved before this sub-project fails to load.
- A note-driven render differs between block sizes.

## 4. Director decisions (2026-10-28)

| # | Question | Decision |
|---|---|---|
| D1 | What notes do in the instrument | **Loading is silent; notes start the sound.** While any note is held, the Cloud stream reads the loaded sample, repitched to the notes. |
| D2 | Chords, before M3's Voice scheduler | **One cloud; each grain picks one held note at random** at its onset, from the engine's counter RNG. Not mono, and not polyphony pulled forward from M3. |
| D3 | How a sample gets in before the Perform UI | **A minimal loader in today's editor:** drag and drop, a "Load…" button, the file name, a "missing" state with relink. Sub-project 5 moves the drop onto the canvas. |
| D4 | What a note does in the FX | **Hold and play:** note-on freezes the live ring and repitches the grains to the held notes; releasing every note unfreezes it. Not a snapshot capture, and not a freeze without repitch. |
| D5 | A sample missing on reopen | **Search, then show missing with relink** (§6.7). Not embedding user samples, which §18 rules out. |
| D6 | The details proposed in the conversation | **Accepted as proposed** ("this all seems fine"): note 60 plays the sample at its own pitch; velocity does nothing; the Freeze parameter or any held note freezes the FX; a note-on engages even right after a reset; WAV, AIFF, FLAC and MP3; conversion to the host rate with r8brain; a 10-minute cap; the instrument's Position as 0–100% of the file and Spray in seconds; a new sample swaps in while old grains finish on the old one; `gg-render`'s instrument mode, notes and WAV input. |
| D7 | How the state carries the manifest | **§18's chunked container, now** (§5). |
| D8 | A render made right after a project opens (raised by the builder check) | **The sample is ready before the session restore returns** (§6.4), so every render after opening includes it. Opening a project with a large sample takes a few seconds longer. Not a background load with a documented race. |

## 5. Approaches

The design has one real fork: how the session state carries the media manifest. The rest follows
the project's existing pattern (the loader in a JUCE-free library shared by the plugin and
`gg-render`; a plugin-owned background thread; a pointer swap to the audio thread; note handling in
the `Processor` on the control grid; the per-grain note drawn by the scheduler).

1. **§18's chunked container, now (chosen, D7).** The state becomes a small binary container: a
   `PTCH` chunk holding today's patch XML unchanged, and a `MEDI` chunk holding the manifest.
   `AUDI` is reserved and not written. A state saved before this change, bare XML, still loads.
   - *Costs:* a container format, its versioning and tests now.
   - *Gains:* the format changes once before release, not twice; M2's embedded audio never passes
     through XML; unknown chunks can be skipped, so later chunks don't break older builds.
2. **The manifest inside the patch XML.** Least work now, but M2 must still introduce a container
   for binary audio, so the format and its migrations change a second time.
3. **A sidecar file beside the project.** Ruled out: neither CLAP nor VST3 tells a plugin where the
   project lives.

## 6. Design

### 6.1 Notes, shared by both products

- **Input.** CLAP note-on and note-off events, CLAP's note choke (treated as a note-off), and the
  MIDI dialect (note-on, note-off, note-on with velocity 0 as a note-off, CC 123 "all notes off").
  MPE input works as plain notes: its per-note channels are just channels. A CLAP note-off with
  wildcard (−1) fields releases every held note it matches.
- **The control grid.** A note event at absolute sample a takes effect at the grid boundary
  b = ceil(a / 32) × 32, exactly as a parameter event does (INTENT §14), and joins that boundary's
  group. A group's note events apply in order, and only the held set after the group counts. So a
  note that starts and ends within one group, at most 32 samples long, does nothing. That rule is
  what keeps note timing block-size invariant.
- **The held set** is the set of held (channel, key) pairs. A key held on two channels counts once
  when grains pick; it is released when both are off. A `Processor` construction or `reset()`
  clears it.
- **Each grain's note.** At each onset while the held set is non-empty, the scheduler draws
  u = uniform01 keyed by (seed, layer, stream, a new `rng::Field::Note`, k), takes the distinct held
  keys in ascending order, and picks index floor(u × n). Every grain draws, so the draw doesn't
  depend on how many notes are held.
- **Pitch.** A grain's pitch in semitones is the effective Pitch parameter plus (key − 60), clamped
  to the scheduler's [−48, 48]. Note 60 (C3 in Live's naming, C4 in others') plays the source at
  its own pitch. With no note held, the pitch is the Pitch parameter alone, as today.
- **Velocity** is ignored (D6).

### 6.2 The instrument

- **Silent until a note.** While the held set is empty, no grains spawn. That is the empty
  instrument's behavior today, so an instrument with a sample and no notes sounds as one without a
  sample does.
- **Note-on starts the cloud at once.** When the held set goes from empty to non-empty at boundary
  g, the next onset is g: the first grain spawns at the note's own boundary, never after an interval
  drawn while nothing was held. The grain index k keeps counting across notes, so two identical
  phrases don't repeat each other's draws.
- **Adding a note to a held chord** doesn't restart the cloud; the new key joins the pick set from
  the next onset.
- **Release.** When the held set empties, no grain spawns from that boundary on. Grains already
  sounding play out their own window: the release tail is as long as Size, with no click. A proper
  amplitude envelope is M3's.
- **Position** in the instrument is 0–100% across the loaded file, default 0% (§6.8). Spray is
  seconds, 0–2 s, bipolar around Position, so its spread doesn't change with the file's length.
- **Mix** stays hidden and inert (ruling M1-w). **Freeze** does nothing in the instrument (it has no
  live ring), as today.
- Everything else (Size, Density, Distribution, Stereo, Level, Pitch) works as in the FX.

### 6.3 The FX: notes hold and play (D4)

- **The engine's freeze is on while the Freeze parameter is engaged or any note is held.** Notes
  never change the Freeze parameter, its automation or its saved state.
- **Engaged** keeps its meaning from §23 #40 and ruling M1-ac: the parameter engages only on an
  off→on transition after an activation or reset. **A note-on needs no arming:** it engages even
  right after a reset or a reopened project, because a note is a deliberate act and by then the ring
  is filling (D6).
- **Note-on** freezes with the existing 20 ms write-gain ramp and the admission clamp (ruling M1-p),
  and grains spawned while notes are held take their pitch from the notes (§6.1). Position and
  Spray keep reading seconds behind the (now stopped) head.
- **A note-on starts a grain at once, as in the instrument** (§6.2; Claude's call, from the builder
  check). When the held set goes from empty to non-empty at boundary g, the pending onset becomes
  g, so the first repitched grain sounds at the note's boundary rather than up to 1/Density later
  (4 s at the 0.25 Hz floor). Adding a note to a held chord doesn't move the onset. One gate
  mechanism serves both products; ruling M1-h's rescale is unchanged for Density and Distribution
  changes.
- **Releasing every note** unfreezes the ring (the same 20 ms ramp back) unless the parameter is
  engaged, and new grains go back to the Pitch parameter alone.
- **Legato and staccato differ, on purpose.** Overlapping notes keep the ring frozen on what it
  first held. Detached notes unfreeze between them, so each new note holds fresher audio.
- **No note-triggered capture buffer** (D4's option 2) and nothing new is saved.

### 6.4 Loading a sample

- **Formats,** decoded with the pinned dr_libs: WAV (PCM 8–32-bit and float), AIFF/AIFC, FLAC and
  MP3. Mono or stereo; a file with more channels uses its first two. Anything else fails with a
  message the editor shows.
- **Length cap: 10 minutes** of audio per instance in M1, measured as the file's duration
  whatever the host's rate. A longer file is refused with a message. At 48 kHz a stereo minute is
  about 23 MB planar, and about 46 MB with the interleaved copy the engine reads (§23 #46), so a
  10-minute source is about 460 MB; at 96 kHz about 0.9 GB. M2's memory cap replaces this limit.
- **At most three sources exist at once** (Claude's call, from the builder check): the one
  draining, the one playing, and one in flight. A new load request supersedes one that is still
  waiting or being built, which is abandoned. The decoded original isn't kept: a rate change
  decodes the file again. So the worst case at 48 kHz is about 1.4 GB, briefly, for 10-minute
  files.
- **Sample-rate conversion on load,** with r8brain-free-src (MIT; INTENT §15 already lists it). It
  becomes a pinned submodule. The loader converts the decoded file to the host's sample rate. When
  the instrument is activated at a different rate, the loader converts again in the background.
  Until the new buffer arrives, the old one keeps playing at the right pitch, because the engine
  already scales each grain's rate by source rate / output rate.
- **The fingerprint** is the SHA-256 of the file's bytes, with the file's size beside it, so a
  search can skip candidates by size before hashing.
- **Off the audio thread.** A plugin-owned background thread (one per instance) decodes, converts,
  hashes, builds the immutable source (the planar buffer and its interleaved copy) and touches
  every page. The main thread hands the finished source to the audio thread through a lock-free
  single-slot mailbox. The audio thread adopts it at the start of a `process()` call, and hands the
  old one back through a second slot, to be freed on the main thread (§14 "Loading"). `process()`
  never allocates, frees, locks or does I/O for any of this.
- **Restoring a session is synchronous (D8).** `stateLoad` finds, verifies and decodes the
  manifest's sample before it returns, on the main thread, where hosts expect state loads to take
  time. When the plugin is active, `stateLoad` also converts the sample to the current rate and
  posts it through the mailbox together with the loaded parameters, so the audio thread adopts
  both at the same `process()` call. When it is inactive, `activate()` does the conversion, also
  on the main thread, before it returns. So the first block a host renders after opening a project
  already has the sample, and two exports of one project are identical. A sample that can't be
  found leaves the instrument silent and the status *missing*, deterministically. Loads by button,
  drop or relink stay in the background.
- **The swap doesn't click.** Grains that started on the old source finish on it, and grains from
  the next onset on read the new one. The engine keeps the old source alive until its last grain
  ends. A further load arriving while two sources are in play waits in the mailbox until the oldest
  one drains, at most Size later (2 s).
- **The engine's `setSource` is split** into a non-real-time step that builds the immutable source
  (the planar `SampleBuffer` and its `InterleavedStereo` copy) and a real-time-safe adoption of a
  ready source. The kernels' bits don't change.
- **Windows paths.** Every file the loader opens uses dr_libs' wide-character (`_w`) entry points
  on Windows, and paths are stored as UTF-8 (carry-over B-m9).

### 6.5 The minimal loader in the editor (D3)

Today's placeholder editor gains a strip, with no other UI changes:

- a **"Load…"** button (a file chooser filtered to the four formats) and a **drop target** over the
  whole editor;
- the **file name**, and a status: *no sample*, *loading…*, *loaded* (with length, channels and
  file sample rate), *missing* (with **"Relink…"**), or an error message;
- a **"Sample folder…"** button that sets the per-machine sample folder (§6.7).

The FX shows none of this: it loads no samples.

The editor talks to the plugin on the main thread and polls the status as it already polls the
note-on counter. Sub-project 5 replaces this strip with the canvas's drop target and its own
status display.

### 6.6 The state container (D7)

- **Layout.** The magic `GGST`, a little-endian u32 container version (1), then chunks. Each chunk
  is a four-character ID, a little-endian u64 payload length, and the payload.
- **Chunks in this version:**
  - `PTCH`: today's patch XML, byte for byte (patch version 1, unchanged).
  - `MEDI`: the manifest, as XML: one `<sample>` entry per loaded sample (zero or one in M1) with the
    SHA-256, the file size, the absolute path (UTF-8), the path relative to the sample folder when
    the file was inside it, the file's format, channels, sample rate and frame count, and
    `embed="0"` (§18's embed flag; always 0 until M2).
  - `AUDI` is reserved for M2 and not written.
- **Loading:**
  - A state that begins with `<` is a pre-container state: its whole content is `PTCH`, with an
    empty `MEDI`. Every state saved by sub-projects 1 and 2 loads this way.
  - Unknown chunk IDs are skipped, so a newer build's chunks don't break this one.
  - All or nothing, as today: a bad magic, a truncated chunk, a missing `PTCH`, or a `PTCH` or
    `MEDI` that fails to parse rejects the whole state and changes nothing.
  - **The read is capped at 64 MiB** (carry-over T7 218), above M2's 32 MB embed limit; a longer
    stream fails cleanly.
- **`MEDI` records the requested sample, not the one playing** (Claude's call, from the builder
  check). The manifest lives on the main thread. A load or relink replaces it; nothing else does.
  A save while the sample is missing, still being searched for, or loading writes the saved entry
  unchanged, so opening and saving a project on a machine without the library never erases the
  reference.
- **The FX** writes a container too, with an empty `MEDI`, so both products keep one format.
- **The patch's Position across products.** Both products share one state format (ruling M1-w),
  but Position now means seconds in the FX and a fraction of the file in the instrument. A state
  moved between products keeps the stored value and clamps it to the other product's range. No
  instrument state has ever made sound, so no migration is needed.

### 6.7 Finding a missing sample (D5)

- **Per-machine settings,** in a small XML file in the user's configuration folder
  (`~/Library/Application Support/Giga Grains/` on macOS, `%APPDATA%\Giga Grains\` on Windows,
  `$XDG_CONFIG_HOME/giga-grains/` on Linux): the **sample folder** the user sets once, and the
  **last folder loaded from**, updated on each load. Main thread only.
- **On a state load,** the manifest's sample is looked for inside `stateLoad` (D8), in this order:
  1. its absolute path;
  2. the sample folder plus its stored relative path (this is what finds samples on a moved or
     renamed drive, or on another machine with the same library);
  3. by file name, in the original file's folder, then the last folder loaded from, then
     recursively under the sample folder. That scan visits at most 50,000 entries.
- **Every candidate is checked** by size, then SHA-256. The first match loads. A match away from the
  saved path updates the manifest and marks the state dirty (CLAP `state.mark_dirty`), so the next
  save records the new place.
- **Nothing matches:** the status is *missing* and the instrument stays silent. A file at the saved
  path whose content differs counts as a non-match, and the status says the file changed.
- **Relink** opens a file chooser. A file with the matching fingerprint restores the sample. A
  different file loads as a replacement, and the status says so.
- **`gg-render` never searches:** a render spec names its file directly.

### 6.8 Parameters

- **No new parameters and no new IDs.** The ten IDs of §18 are unchanged.
- **The instrument's Position** is declared 0–100% (CLAP value 0–1), default 0%. The FX's stays
  0–16 s. `ParamMap`'s metadata becomes per product. Carry-over T6 (152) is closed by this.
- **Spray** keeps its 0–2 s range in both products. In the instrument it is bipolar around Position,
  in seconds, converted to a fraction of the file at the draw (the engine knows the file's length).
  `gg-render`'s sample mode keeps M0's fractional `positionJitter`, so the M0 and Eco goldens don't
  move.

### 6.9 `gg-render`

- **Instrument mode** (`"mode": "instrument"`) runs Giga Grains' `gg-core` `Processor` over a
  sample: `"sample"` is a file path (relative to the spec) or a built-in `sine`, `sweep` or `noise`
  source. It takes `params` and `automation` as FX mode does, and `notes`.
- **`notes`,** in both instrument and FX mode: a time-ordered list of
  `{"time", "key", "duration"}` (seconds, 0–127, seconds), with an optional `"channel"` (0–15,
  default 0). Overlapping notes are chords.
- **FX mode's `input`** accepts `"kind": "file"` (ruling M1-aa). The file is converted to the
  render's rate with the same loader, then faded in and out over 10 ms as the built-in inputs are
  (ruling M1-v).
- **New golden cases:** a single note; a three-note chord (the pick); a release tail; a 44.1 kHz
  file rendered at 48 kHz (the conversion); FX note freeze, from note-on to release and back to
  live; an FX file input.
- **Test audio.** The cases' input files are generated by the test build rather than committed,
  so no audio binaries enter git. If that proves awkward, one tiny CC0 file under `tests/golden/`
  is acceptable.

## 7. Determinism and real-time safety

- **Notes are events on the grid,** applied in boundary groups, so note-driven output is
  bit-identical at block sizes 1, 32, 64, 511 and 4096 and under random splits. The determinism
  tests gain instrument and FX note renders.
- **The note pick** has its own `rng::Field`, so adding it changes no existing draw: every existing
  golden's event log stays identical.
- **Sample-rate conversion** happens before rendering, off the audio thread. Its output is
  deterministic on one platform. Across platforms, the converted buffer's length is identical, so
  the event logs match; the audio metrics compare within tolerance as usual. r8brain compiles with
  the project's `-ffp-contract=off`.
- **What `process()` does for a load:** reads one atomic pointer, swaps two, and nothing else. The
  `[rt]` tripwire tests cover adoption and hand-back. Carry-over [144] is done first (ruling R7's
  fallback under `GG_SANITIZE`), so ASan keeps its new/delete mismatch checks while the loader
  lands.
- **The search, the hash and the settings file** never run on the audio thread, and nothing they
  produce changes how a given buffer renders.

## 8. Verification

- **Unit tests:**
  - the held set: on, off, velocity-0 note-on, choke, wildcard note-off, CC 123, two channels on one
    key, `reset()`;
  - gating: the first onset at the note's boundary, k continuing across notes, no spawn after the
    release boundary, a sub-grid note doing nothing;
  - the pick: the distribution over held keys, its determinism, its independence from other draws;
  - the FX: the parameter OR notes; a note-on engaging right after a reset; release restoring
    live input; Freeze's saved value untouched by notes;
  - the container: round-trip, pre-container XML, unknown chunks, truncation, a bad magic, the
    64 MiB cap, all-or-nothing, and `migratedFromVersion` and `dirty` in the bad-state snapshot
    (carry-over T5 105);
  - the manifest: round-trip, UTF-8 paths;
  - the search, in per-test folders under `GG_TEST_OUTPUT_DIR`: each of the three steps, the size
    pre-check, a changed file, the entry limit;
  - the loader: each format, channel handling, the cap, conversion length and accuracy (a 44.1 kHz
    sine converted to 48 kHz), SHA-256 against the NIST vectors;
  - `SampleBuffer` non-copyable, with a moved-from buffer at 0 × 0 (carry-over [73]).
- **`[rt]` tests:** source adoption and hand-back under the allocation tripwire; note events under
  the tripwire.
- **Plugin tests,** through the CLAP factory: a state with a manifest round-trips; the first
  `process()` after a `stateLoad`, active or followed by `activate()`, already renders the sample
  (D8); the AU state
  format is pinned (carry-over, "now due"), together with clap-wrapper's preset-name and
  save/restore result issues (T7 223); port-value tests and the FX's no-input branch (C-m4, T7 219).
- **Golden renders** for every case in §6.9. Every spectrogram is looked at before the goldens are
  committed.
- **Validators:** clap-validator, pluginval, auval (all three identities) and Steinberg's validator
  pass with the binary state.
- **Gate:** `scripts/check.sh` (dev, asan, rtsan) and CI green.

## 9. Listening and host checklist (feeds listening gate #1, sub-project 6)

A/B renders: a single note at 60, 48 and 72; a chord; the release tail; the 44.1 kHz file; FX note
freeze over a drum loop. Notes far above 60 alias until M2's mipmaps; the checklist asks how far up
it stays usable.

In Ableton Live 12 and Bitwig Studio:
- load by button and by drop; play notes and chords; check the release tail;
- load a new sample while notes play, with no click;
- save, reopen, and hear the same sample; move the file into the sample folder, reopen, and see it
  found; delete it, reopen, see *missing*, relink;
- FX: route MIDI into it (Live's "MIDI To", Bitwig's note input), hold notes over a playing track,
  release, play legato and staccato;
- ruling M1-ac's lines: a project reopened with Freeze on plays live; toggling engages; stop and
  start in a VST3 host; Freeze turned on while suspended.

## 10. Carry-over items

**Owned by this sub-project, and closed by it:**
- M1-aa: FX-mode WAV input (§6.9).
- M1-ac: INTENT §14 and §23 #40 state Freeze's full arming rule (as `Processor.h` has it), and the
  host checklist gains its lines (§9).
- T6 (152): the instrument's Position mapping (§6.8).
- T7 (218): the state read cap (§6.6).
- [73]: `SampleBuffer` move semantics (§8).
- [144]: the tripwire's replacement `new`/`delete` compiled out under `GG_SANITIZE`, before the
  loader lands (§7).

**Owned, but staying open:** T4 (127) and T4 (128), the source-mode switch. No product switches
source mode in this design (the instrument stays Sample, the FX stays Live), so the switch stays
unreachable. Both rows move to M2, where the resample source and the layers can switch.

**Due because this sub-project touches the area:**
- B-m9: wide-character paths on Windows (§6.4).
- [119]: the dr_libs and stb implementation units move into a third-party target without
  `gg::compiler-options`, since the loader adds more of dr_libs.
- T5 (105): the bad-state snapshot (§8).
- The AU state-format pin and T7 (223) (§8).
- C-m4's port-value tests and T7 (219) (§8).
- `gg-render`: T8 (207) (a mono FX check against a direct `Processor` run), T8 (208), T8 (209),
  [181] (a zero-frame render), [182] (an internal-error exit code) and [184] (`bad_alloc` around
  the metrics).

## 11. INTENT changes (Part II, with §23 entries in the same change)

- **§14:** the notes rules (§6.1–§6.3) under "Cloud core rules"; under "Buffers and memory", the
  loader's specifics: conversion on load and again on a rate change, the M1 10-minute cap, and the
  swap that lets old grains finish.
- **§14 and §23 #40:** Freeze's full arming rule (ruling M1-ac).
- **§15:** r8brain-free-src's pin; where SHA-256 comes from (§12).
- **§18:** the container's layout, `MEDI`'s fields, the search order and the per-machine settings;
  the instrument's Position range. "Loading: sessions load quickly and decode audio asynchronously"
  becomes: a session restore loads its samples before it returns, so renders after opening are
  complete (D8); interactive loads decode in the background.
- **§23:** one entry recording D1–D8, dated 2026-10-28.

Part I is unchanged. §4's "possibly several at once" and §5's minutes-long samples stay true, as
targets that M2 reaches.

## 12. Left to the builder

- Whether the loader, the container, the manifest and the settings live in `gg-core` or in a new
  JUCE-free library beside it. Either way, no JUCE, and both the plugin and `gg-render` use it.
- The SHA-256 implementation: in-house, tested against the NIST vectors, or a pinned
  AGPLv3-compatible header.
- How the engine keeps the old source alive for its grains (a per-grain source slot is the obvious
  way), so long as the kernels' bits and the §16 benchmark don't regress.
- How the scheduler learns the gate (a gate call on the `Engine`, or density off and on with the
  onset forced to g), so long as §6.2's timing holds exactly.
- Whether the grain-event line gains the picked key. `rate` already shows it; adding a field would
  change every existing event log.
- The JUCE widgets and layout of the loader strip.
