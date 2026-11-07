# M3 Playable: the program design

| | |
|---|---|
| **Status** | Draft for the director's approval (brainstorming, 2026-11-07). The builder check's eight questions are answered in §2a as **Claude's provisional calls** (the director asked, 2026-11-07, that work continue overnight); each stands until the director overrules it |
| **Milestone** | M3 (Playable), tagged 0.3.0 as an unpublished draft release at its close (INTENT §20, §23 #62) |
| **Depends on** | M2 closed (§23 #93); `main` at 0.3.0-dev (`a3d5f2a`) |
| **Authority** | INTENT.md. This document places every M3 deliverable in a sub-project and records the director's decisions from brainstorming. Each sub-project then gets its own spec, plan, build and review, as M1's did (§23 #35). M3 runs as M1 did: one session, sub-projects in series, parallel tasks inside each (§23 #87). Part II changes below each add a dated §23 row |

## 1. Intent

M3 makes Giga Grains **playable** (INTENT §3's second pillar: "pitched, polyphonic,
MIDI/MPE-expressive"). Today a note owns nothing: every grain draws its key at random from the
instance's held set, the instrument stops spawning grains at a note-off, and two notes on one
(channel, key) share one held bit (carry-over rows 404 and 184). At M3's close a note is a
**voice**: its own grain stream, envelopes, expression and release, played from a LinnStrument or
a keyboard. §20 says why Playable comes first: it gives the instrument its own identity, and the
Voice scheduler is the groundwork for M4's Sync.

**Who plays it** (the director, 2026-11-07): "a linnstrument or a keyboard." So:
- **The LinnStrument:** MPE is first-class from the first sub-project. Per-note pitch slides (its
  default bend range is ±48 semitones, so the range is read, never assumed), Y-axis timbre
  (CC 74), pressure and release velocity reach that note's voice alone, and a slide is followed
  continuously while the voice sounds.
- **The keyboard:** the sustain pedal (CC 64), channel-wide pitch bend, mod wheel and aftertouch
  (already matrix sources), and polyphony enough for pedaled chords with long releases.

## 2. Director decisions (2026-11-07, in brainstorming)

1. **Voice is a scheduler mode per layer, beside Cloud** (option A of two). Each layer gets a
   **Mode** parameter: **Cloud** (today's behavior, the default) or **Voice** (M4 adds Sync to the
   same parameter). Declined: (B) the instrument always plays voices, with Cloud what each voice
   runs, which would move every instrument golden and leave the FX a special case.
2. **The voice model as played back** (§4), approved ("looks great").
3. **The default expression routing** (§5): pressure to Density and a little Level, timbre to
   Position, velocity to Level and a little Size. The director's instinct was "density shift
   probably", with the musical call left to Claude; the addition of Level (pressure is the
   LinnStrument's dynamics, and Density alone is level-neutral) was approved ("yes").
4. **Pitch-sync, microtuning and per-voice modulation as played back** (§6, §7, §4.4), approved.
5. **The order: a playable slice first** (option A of three, "go with recs"): six sub-projects,
   §8. Declined: (B) engine first, then input, then sound, playable only from its third
   sub-project; (C) three big sub-projects, the first huge.
6. **Row 405 is fixed first,** as its own small pull request, before sub-project 1 ("go with
   recs").

## 2a. Claude's provisional calls (2026-11-07, after the builder check)

The director, going to sleep: "please progress this as much as possible while i'm asleep." The
builder check raised eight questions only the director can settle; each is answered here by
Claude, provisionally, and work proceeds on them. **P1 and P2 are the ones most likely to be
overruled.**

- **P1. The instrument's INIT stays at Mode Cloud.** The parameter's default is Cloud in both
  products, so INIT, the 48 factory presets and every golden are unchanged, and row 404 closes for
  Voice layers: the instrument as installed still stops spawning at a note-off until a patch
  switches to Voice. Sub-project 5's new Voice presets are the playable defaults. *Alternative the
  director may choose:* the instrument's Mode defaults to Voice (INIT plays voices; the M2 golden
  specs and the 24 instrument preset sources then name Mode Cloud explicitly, so their renders
  keep their hashes).
- **P2. MPE is an instance setting, Off by default.** Three global parameters, not automatable,
  which a preset load keeps (as it keeps the ring parameters): **MPE** (Off, On), **MPE Bend
  Range** (1 to 96 semitones, default 48: the MPE spec's member default; a LinnStrument set to
  ±24 sets 24) and **Bend Range** (0 to 24 semitones, default 2: a keyboard's wheel and the MPE
  master channel). With MPE Off a channel's pitch bend bends every Voice voice on that channel by
  Bend Range, so a plain keyboard's wheel never bends four octaves. With MPE On, channel 1 is the
  lower zone's master (its bend, pressure and CCs reach every voice) and channels 2 to 16 are
  members (theirs reach that channel's voice alone). Sub-project 2 adds the MPE Configuration
  Message and RPN 0, which override the setting in effect. CLAP note expressions work whatever
  the setting. Cloud layers take no bend directly (pitch bend stays a matrix source there); a
  user's own Pitch Bend → Pitch route on a Voice layer adds to the direct bend.
- **P3. The input paths are sub-project 1's, all three** (the wrapper research,
  2026-11-07): (a) CLAP notes with note IDs and CLAP note expressions (tuning in semitones,
  pressure, brightness, volume, pan) — Bitwig's CLAP sends MPE as note expressions to a plugin
  preferring the CLAP dialect, which ours does, and the preference stays; (b) the MIDI-MPE
  dialect (per-channel bend, CC 74, channel and poly pressure, CC 64, release velocity), which
  VST3 delivers through clap-wrapper's IMidiMapping as raw MIDI; (c) `clap_plugin_as_vst3` with
  the tuning, volume, pan and brightness note expressions and 16 MIDI channels, feeding (a). AU
  MPE is blocked by the wrapper (no `kAudioUnitProperty_SupportsMPE`; under the CLAP dialect its
  bend arrives as a ±2 channel-wide tuning expression): the AU plays voices polyphonically, with
  channel-wide expression only, recorded as carry-over rows, nothing filed upstream (§23 #50).
  So sub-project 1's exit is a LinnStrument per note in Bitwig (CLAP and VST3) and Live (VST3,
  if Live sends per-note MIDI or note expressions to it; a host test decides), and a keyboard in
  every format.
- **P4. Voice-only routes.** A route gains a **Voice only** flag (saved as an optional attribute,
  absent meaning off, so every saved patch reads as before): a Voice-only route is skipped on
  Cloud layers. The new factory routes, slots 13 to 17, are all Voice only: Pressure → Density,
  Pressure → Level, Timbre → Position, Velocity → Level, Velocity → Size. (§5's "velocity to Level,
  as now" was wrong: no factory route uses Velocity today; the twelve are the macros'.) So no Cloud
  render moves, and INIT switched to Voice plays with the approved expression at once.
- **P5. CLAP per-note parameter modulation** (`CLAP_PARAM_IS_MODULATABLE_PER_NOTE_ID` and the
  per-key, per-channel flags) is sub-project 2's, on the CLAP path only; the VST3 and AU wrappers
  force incoming parameter events to note ID −1, a carry-over row.
- **P6. Voice handling is in-house, not sst-voicemanager.** The library (MIT, `01b617c`,
  2026-10-13) has no MPE zones, Configuration Message, RPN bend ranges or counted (channel, key)
  identity, ranks pedal-held voices as gated (our steal order treats them as released), keeps
  per-port state in hash maps of about 560 KB, and fetches its dependency at `main`. Its steal
  order and note-ID stack inform the design; §15's planned row is removed with a §23 row.
- **P7. A per-voice source routed to a target that isn't per voice** (a global such as Reverb
  Amount or Morph, or a Cloud layer through `sourceLayer`) reads **the newest sounding voice's
  value**: last-note priority, as Velocity and Key read the last note today. With no voice
  sounding it reads 0.
- **P8. The pedal and note identity reach Cloud layers and the FX too:** CC 64 holds the held
  set (the instrument's note gate; the FX's note freeze), and two notes on one (channel, key) are
  counted, so the first note-off no longer releases both (row 184 for every layer). No golden
  sends CC 64 or a duplicate note, so none moves; "a Cloud patch sounds the same" (§3's anti-goal)
  means at rest and for the inputs M2 handled.

## 3. Goals, non-goals, anti-goals

**Goals.**
- Every item of §20's M3 row built: the Voice scheduler, polyphony, MPE, note expressions,
  polyphonic modulation, per-voice envelopes, pitch-sync and formant grains, microtuning.
- Carry-over rows 184 and 404 closed by the Voice scheduler.
- **At rest, nothing moves:** with every layer at Mode Cloud and the tuning 12-TET, both products
  render bit-identically to M2. All 108 goldens (the 60 cases and the 48 preset goldens) keep
  their WAV SHA-256 through the milestone, except where a commit says why one moved (§9.2).
- No CPU regression on the §16 headline case (X, `engine-sample`) against the committed
  baselines: Cloud mode's hot path is unchanged.
- The director plays it on a LinnStrument in Live and Bitwig at the end of sub-project 1.
- Listening gate #3 at the close; the first-class hosts pass (§20's exit).
- 0.3.0 tagged, its draft release kept unpublished (§23 #62).

**Non-goals.**
- Mono, legato and glide (neither instrument needs them; they fit later).
- Per-voice LFO phase (LFOs stay global; a later option).
- M4's Sync scheduler, M5's oscillators, synthetic grains and per-grain filters.
- Anything public, and freezing parameter IDs (that is M5's first beta, §18; IDs stay
  append-only regardless).
- signalsmith-stretch (§15 plans it for the FX's transparent pitch shift; nothing in M3 needs it).

**Anti-goals** (a failure even if everything works).
- A voice that clicks when it starts, is stolen or ends.
- Output that depends on block size, host, CPU load or the order events arrive within a group.
- An allocation, lock or syscall on the audio thread, MTS-ESP's queries included.
- A light touch on the LinnStrument that doesn't speak: MPE pressure starts at 0 at a strike, so
  the base sound must stand on its own.
- A Cloud-mode patch that sounds different after M3.

## 4. The voice model

### 4.1 Mode and voices
- **Mode** (per layer, stepped): Cloud (0, default) or Voice (1). M4 appends Sync. At Cloud a
  layer renders exactly as in M2: grains pick keys from the held set, the note gate, D4's notes
  freezing the FX.
- **At Voice, every note-on starts a voice on that layer.** A note reaches every Voice layer, as
  notes reach every layer today (per-layer note routing stays later).
- **A voice owns** its own grain clock and stream (the layer's parameters are its base values,
  its own modulation on top), its pitch, its envelopes, its expression and its release.
- **Note identity:** a voice is addressed by its CLAP note ID when it has one, else by (channel,
  key) with a count, so two notes on one (channel, key) are two voices and the first note-off
  releases one (closes row 184). The held set Cloud layers read is derived from the voices (a key
  held while any voice on it is held).
- **Polyphony:** a fixed cap of **16 voices per layer** (Claude's proposal, accepted with §4). A
  note-on past the cap steals deterministically with a fade: the oldest released voice first,
  else the oldest held. The fade is the overload ladder's raised-cosine steal (§14).
- **The sustain pedal (CC 64):** a voice whose note is released while the pedal is down stays in
  its sustain until the pedal comes up. It applies to Cloud layers' held set too (the instrument's
  note gate), since a pedaled chord should hold in both modes.

### 4.2 Pitch
A voice's pitch is the layer's Pitch plus the note's pitch: its key through the tuning (§7), plus
the channel's pitch bend, plus its per-note bend (MPE) or CLAP tuning expression. It is followed
continuously: a slide moves the grains' read rate per grain at least, per sample where a grain's
rate is ramped (the sub-project's spec decides).

### 4.3 Envelopes and the release
- **Env 1 and Env 2 become per voice** on Voice layers: each voice runs its own ADSR, gated by its
  own note (and the pedal). On Cloud layers they stay as today, gated by the held set.
- **The stream continues through the release** (closes row 404): after a note-off the voice keeps
  spawning grains while its envelope releases, and ends when the release has finished and its last
  grains have played out their windows.
- **What ends a voice** (which envelope is its amplitude, and whether one is implied) is
  sub-project 1's forced choice for the director (§10).

### 4.4 Per-voice modulation
- The mod matrix is evaluated **once per voice** on Voice layers. The per-voice sources take each
  voice's own values: Env 1, Env 2, Velocity, Key, and the new **Pressure**, **Timbre** and **Note
  Pitch** (and **Release Velocity**, unrouted by default). The global sources (macros, LFOs,
  Random, Step, the follower, transport, mod wheel, channel aftertouch and pitch bend) are shared.
- On Cloud layers the per-voice sources read as today (Velocity and Key the last note's; the new
  ones 0), so no Cloud render moves.
- New sources are appended to `ModSource` (saved by name, so the order is free to grow).

### 4.5 Determinism
- Each voice's grains are keyed by the counter RNG with a voice field: (seed, layer, stream,
  field, voice ordinal, grain index), the voice ordinal counting note-ons on that layer from
  activation. Cloud layers' keys are unchanged.
- Voices are summed in a fixed order, by start (ordinal), inside each layer; layers in A to D.
- Note events stay on the 32-sample control grid (§14), so voices start, release and steal at the
  same sample at any block size.
- The CPU budget's per-layer quota is shared by a layer's voices (§14's "per-voice quotas"); how
  it divides is sub-project 2's.

## 5. Expression and its default routes

- **Input, one per-voice path** (§15): the MIDI-MPE dialect (VST3, AU, CLAP's MIDI dialect;
  per-note channels, the MPE Configuration Message and RPN 0 bend ranges, per zone) and CLAP note
  expressions (Bitwig: pressure, brightness, tuning, volume, pan, vibrato, expression). P2 and P3
  say which reach sub-project 1 and how MPE is switched on.
- **The default routes** for Voice layers (the director's decision 3), as ordinary matrix routes
  INIT and the factory routes carry, so a preset may change them:
  - **Pressure to Density** (up from the base: a light touch sparse and audible, pressing in fills
    to a continuous tone; Density's gain normalization keeps it level-neutral) **and a little to
    Level** (pressure as dynamics);
  - **Timbre (Y, CC 74) to Position** (a slide up the pad scans the sample; on a Live or Resample
    layer, back through the ring);
  - **Note pitch** (X slides) as pitch, not a route;
  - **Velocity to Level and a little to Size** (new: no factory route uses Velocity today).
  - Depths are sub-project 1's, set by ear and by renders; M2's twelve factory routes keep slots 1
    to 12 bit for bit, so the new routes take slots 13 to 17, all **Voice only** (P4), and are
    skipped on Cloud layers.
- **The base sound speaks at pressure 0:** the default Density and the depths are chosen so a
  struck note with no pressure is a clear, sparse voice.

## 6. Pitch-sync and formant grains

- A per-layer **Clock** choice inside Voice mode: **Free** (Density, the default) or **Pitch**.
- At Pitch, each voice spawns grains at its note's frequency (220 a second for A3, following
  slides and the tuning): the grain rate is the note heard. The layer's Pitch then sets each
  grain's read rate, the **formant**, and Size sets a grain's length in periods (pulsar and formant
  synthesis). Distribution adds jitter, pure pulse to rough.
- Over a sample: vocal, formant-like tones. In the FX, over the live input: a played note becomes a
  pitched resonance of what comes in, the MIDI pitch-sync feature §23 #25 and #32 promised the FX.
- Rates beyond Density's 400 Hz ceiling are within the scheduler's clock range (to the sample
  rate, §14); the budget's weights still bound a voice's cost.

## 7. Microtuning

- How a key becomes a pitch, for both modes and both products, in order of precedence:
  1. **MTS-ESP**, when a master is connected (the MTS-ESP client, 0BSD-style);
  2. else a **Scala `.scl` and `.kbm`** the user loads (tuning-library, MIT), saved with the
     session; a preset may carry one (how a preset load treats the instance's tuning is
     sub-project 4's);
  3. else **12-TET**, exactly today's mapping, so nothing moves at rest.
- Read on the audio thread without locks or allocation; files parsed off it and handed over as the
  sample pool hands over samples (§18).
- Both libraries are pinned when added (§15's table already lists them).

## 8. The sub-projects

Each is spec, plan, build, review (§23 #35), in series. Each spec opens with its forced choices
for the director in one message, as M2's did.

| # | Sub-project | Delivers | Exit |
|---|---|---|---|
| 0 | **Row 405** (before M3) | The instrument's Mod view label "Source" fits at 640 × 360 in DejaVu Sans | The editor frame test passes with DejaVu as the only font (locally and in the bootstrap job by a dispatch); row 405 closed |
| 1 | **Voice slice** | Mode; voices with their own streams, Env 1 and Env 2 per voice, the release that keeps spawning (rows 404, 184); note identity by ID or (channel, key, count); 16 voices a layer with the simple steal; the sustain pedal (P8); the three input paths (P3): CLAP note expressions, MIDI-MPE and `clap_plugin_as_vst3`; MPE, MPE Bend Range and Bend Range (P2); the new per-voice sources; the Voice-only flag and factory routes 13 to 17 (P4); `NOTE_END` to the host; `gg-render` voice cases | The director plays it on a LinnStrument and a keyboard in Bitwig and Live (informal, not a gate; P3 says which formats carry per-note expression); every M2 golden's WAV hash unchanged; new voice goldens; determinism, RTSan and the sanitizers clean |
| 2 | **Full expression** | The MPE Configuration Message and RPN bend ranges per zone (upper zone too); `voice-info`; the matrix fully per voice (every per-voice source, every target, P7's rule); CLAP per-note parameter modulation (P5); the per-voice budget quotas and the steal by budget; release velocity; the Mod view's Voice-only toggle | MPE and note-expression renders match each other where they mean the same gesture; host checks in Bitwig (CLAP, VST3), Live (VST3, AU) |
| 3 | **Pitch-sync and formant grains** | Clock = Pitch; formant from Pitch, length in periods; the FX's MIDI pitch-sync | Goldens over a sample and an FX input; pitch measured against the note within a tolerance; the budget at high notes |
| 4 | **Microtuning** | MTS-ESP; Scala `.scl`/`.kbm` loaded, saved and restored; the tuning in Cloud and Voice | Renders in a non-12 scale against expected frequencies; 12-TET bit-identical; MTS-ESP checked against a test master |
| 5 | **Views and presets** | The Edit view's Mode, Clock and voice controls; the canvas's grains tinted by voice; the tuning chooser; new Voice and MPE factory presets (appended; preset order freezes only at 1.0, §18) | Every new parameter reachable from the UI (the frame tests); `gg-snap` sets; preset goldens for the new presets |
| 6 | **Close** | The consistency pass; the carry-over burn-down; listening gate #3; the host matrix (first-class hosts, and Logic Pro's MPE and tail question, §22); the idle figures; the 0.3.0 release commit | §20's exit: the gate passed, the first-class hosts pass |

## 9. Rules every sub-project keeps

1. INTENT's non-negotiables (CLAUDE.md): the audio thread, determinism, 64-bit positions, the
   overload order, append-only parameter IDs, licensing.
2. **At rest nothing moves:** Mode Cloud, 12-TET and no new route active render every existing
   golden bit-identically; `scripts/update-goldens.sh` before and after, and the commit says every
   hash matches or names the case that moved and why.
3. New parameters are appended, with patch-version migration where the state format changes
   (each spec says whether it bumps the patch version).
4. Every DSP change ships with unit tests, goldens compared by metrics and the determinism tests;
   `scripts/check.sh` (dev, asan, rtsan) and the validators before claiming done.
5. Each sub-project renders its A/B files for the record; the director's formal listening is the
   close's gate (as M1 and M2). Sub-project 1's hands-on play is the one informal check.
6. **The carry-over:** M2's 15 open rows move to a new `docs/m3-carry-over.md` at sub-project 1's
   start (as M1's moved, §23 #63); `docs/m2-carry-over.md` is frozen as M2's record. Rows 184 and
   404 close in sub-project 1.
7. Models (the director, 2026-10-30): no Fable; opus where the most capable model is called for,
   sonnet for integration and review, haiku for mechanical work.

## 10. Left to each sub-project's spec

- **1:** what ends a voice (Env 1 as the voice's amplitude, or a dedicated amp envelope, or
  neither implied); whether D4's notes-freeze-the-FX applies on FX Voice layers; the new routes'
  depths; whether pitch follows per grain or per sample; the patch version.
- **2:** how the quota divides among voices; which CLAP expressions map to which sources.
- **3:** the period-length range, and how Pitch's formant meets the mip levels at high notes.
- **4:** a preset's tuning on load; the UI for loading Scala files.
- **5:** how many new factory presets, and where they sit in each product's order.

## 11. INTENT changes with this document

- §20's M3 row: the six sub-projects and this document.
- §15: sst-voicemanager's planned row removed (P6); the AU's missing MPE support added to the
  wishes from clap-wrapper upstream.
- §23 #94: these decisions (dated 2026-11-07), the director's 1 to 6 and Claude's provisional P1
  to P8, marked as such.
- Part II text each sub-project changes gets its own row when it is built.
